/* vi: set ts=2:
 *
 *
 * Eressea PB(E)M host Copyright (C) 1998-2000
 *      Christian Schlittchen (corwin@amber.kn-bremen.de)
 *      Katja Zedel (katze@felidae.kn-bremen.de)
 *      Henning Peters (faroul@beyond.kn-bremen.de)
 *      Enno Rehling (enno@eressea-pbem.de)
 *      Ingo Wilken (Ingo.Wilken@informatik.uni-oldenburg.de)
 *
 * This program may not be used, modified or distributed without
 * prior permission by the authors of Eressea.
 */

#include <config.h>
#include "eressea.h"
#include "faction.h"
#include "unit.h"
#include "race.h"
#include "region.h"
#include "plane.h"
#include "item.h"
#include "group.h"

/* util includes */
#include <base36.h>
#include <event.h>

#include <attributes/otherfaction.h>

/* libc includes */
#include <stdio.h>
#include <stdlib.h>

const unit *
random_unit_in_faction(const faction *f)
{
	unit *u;
	int c = 0, u_nr;

	for(u = f->units; u; u=u->next) c++;

	u_nr = rand()%c;
	c = 0;

	for(u = f->units; u; u=u->next)
		if(u_nr == c) return u;

	/* Hier sollte er nie ankommen */
	return NULL;
}

const char *
factionname(const faction * f)
{
	typedef char name[OBJECTIDSIZE + 1];
	static name idbuf[8];
	static int nextbuf = 0;

	char *buf = idbuf[(++nextbuf) % 8];

	if (f && f->name) {
		sprintf(buf, "%s (%s)", strcheck(f->name, NAMESIZE), itoa36(f->no));
	} else {
		sprintf(buf, "Unbekannte Partei (?)");
	}
	return buf;
}

void *
resolve_faction(void * id) {
   return findfaction((int)id);
}

#define MAX_FACTION_ID (36*36*36*36)

static int
unused_faction_id(void)
{
	int id = rand()%MAX_FACTION_ID;

	while(!faction_id_is_unused(id)) {
		id++; if(id == MAX_FACTION_ID) id = 0;
	}

	return id;
}

unit *
addplayer(region *r, const char *email, const char * password, const struct race * frace, const struct locale *loc)
{
	int i;
	unit *u;
	faction *f;

    assert(frace != new_race[RC_ORC]);
    f = calloc(sizeof(faction), 1);

    set_string(&f->email, email);

	if (password) {
		set_string(&f->passw, password);
	} else {
		for (i = 0; i < 6; i++) buf[i] = (char) (97 + rand() % 26); buf[i] = 0;
		set_string(&f->passw, buf);
	}
	for (i = 0; i < 6; i++) buf[i] = (char) (97 + rand() % 26); buf[i] = 0;
	set_string(&f->override, buf);

	f->lastorders = turn;
	f->alive = 1;
	f->age = 0;
	f->race = frace;
	f->magiegebiet = 0;
	f->locale = loc;
	set_ursprung(f, 0, r->x, r->y);

	f->options = Pow(O_REPORT) | Pow(O_ZUGVORLAGE) | Pow(O_SILBERPOOL) | Pow(O_COMPUTER) | Pow(O_COMPRESS) | Pow(O_ADRESSEN) | Pow(O_STATISTICS);

	f->no = unused_faction_id();
	register_faction_id(f->no);

	f->unique_id = ++max_unique_id;

	sprintf(buf, "%s %s", LOC(loc, "factiondefault"), factionid(f));
	set_string(&f->name, buf);
	fset(f, FL_UNNAMED);

	addlist(&factions, f);

	u = createunit(r, f, 1, f->race);
	give_starting_equipment(r, u);
	fset(u, UFL_ISNEW);
	if (old_race(f->race) == RC_DAEMON) {
		race_t urc;
		do
			urc = (race_t)(rand() % MAXRACES);
		while (urc == RC_DAEMON || !playerrace(new_race[urc]));
		u->irace = new_race[urc];
	}
	fset(u, FL_PARTEITARNUNG);

	return u;
}

boolean
checkpasswd(const faction * f, const char * passwd)
{
	if (strcasecmp(f->passw, passwd)==0) return true;
	if (strcasecmp(f->override, passwd)==0) return true;
	return false;
}

void
destroyfaction(faction * f)
{
	region *rc;
	unit *u;
	faction *ff;

	if( !f->alive ) return;

	for (u=f->units;u;u=u->nextF) {
		region * r = u->region;
		unit * au;
		int number = 0;
		struct friend {
			struct friend * next;
			int number;
			faction * faction;
			unit * unit;
		} * friends = NULL;
		for (au=r->units;au;au=au->next) if (au->faction!=f) {
			if (alliedunit(u, au->faction, HELP_ALL)) {
				struct friend * nf, ** fr = &friends;

				while (*fr && (*fr)->faction->no<au->faction->no) fr = &(*fr)->next;
				nf = *fr;
				if (nf==NULL || nf->faction!=au->faction) {
					nf = calloc(sizeof(struct friend), 1);
					nf->next = *fr;
					nf->faction = au->faction;
					nf->unit = au;
					*fr = nf;
				}
				nf->number += au->number;
				number += au->number;
			}
		}
		if (friends && number) {
			struct friend * nf = friends;
			while (nf) {
				unit * u2 = nf->unit;
#ifdef NEW_ITEMS
			  item * itm = u->items;
				while(itm){
					const item_type * itype = itm->type;
					item * itn = itm->next;
					int n = itm->number;
					n = n * nf->number / number;
					if (n>0) {
						i_change(&u->items, itype, -n);
						i_change(&u2->items, itype, n);
					}
					itm = itn;
				}
#else
				resource_t res;
				for (res = 0; res <= R_SILVER; ++res) {
					int n = get_resource(u, res);
					if (n<=0) continue;
					n = n * nf->number / number;
					if (n<=0) continue;
					change_resource(u, res, -n);
					change_resource(u2, res, n);
				}
#endif
				number -= nf->number;
				nf = nf->next;
				free(friends);
				friends = nf;
			}
			friends = NULL;
		} 
		if (rterrain(r) != T_OCEAN && !!playerrace(u->race)) {
			const race * rc = u->race;
			int p = rpeasants(u->region);
			int m = rmoney(u->region);
			int h = rhorses(u->region);

			/* Personen gehen nur an die Bauern, wenn sie auch von dort
			 * stammen */
			if ((rc->ec_flags & ECF_REC_UNLIMITED)==0) {
				if (rc->ec_flags & ECF_REC_HORSES) { /* Zentauren an die Pferde */
					h += u->number;
				} else if (rc == new_race[RC_URUK]){ /* Orks z�hlen nur zur H�lfte */
					p += u->number/2;
				} else {
					p += u->number;
				}
			}
			m += get_money(u);
			h += get_item(u, I_HORSE);
			rsetpeasants(r, p);
			rsethorses(r, h);
			rsetmoney(r, m);

		}
		set_number(u, 0);
	}
	f->alive = 0;
/* no way!	f->units = NULL; */
	handle_event(&f->attribs, "destroy", f);
	for (ff = factions; ff; ff = ff->next) {
		group *g;
		ally *sf, *sfn;

		/* Alle HELFE f�r die Partei l�schen */
		for (sf = ff->allies; sf; sf = sf->next) {
			if (sf->faction == f) {
				removelist(&ff->allies, sf);
				break;
			}
		}
		for(g=ff->groups; g; g=g->next) {
			for (sf = g->allies; sf;) {
				sfn = sf->next;
				if (sf->faction == f) {
					removelist(&g->allies, sf);
					break;
				}
				sf = sfn;
			}
		}
	}

	/* units of other factions that were disguised as this faction
	 * have their disguise replaced by ordinary faction hiding. */
	for(rc=regions; rc; rc=rc->next) {
		for(u=rc->units; u; u=u->next) {
			attrib *a = a_find(u->attribs, &at_otherfaction);
			if(!a) continue;
			if (get_otherfaction(a) == f) {
				a_removeall(&u->attribs, &at_otherfaction);
				fset(u, FL_PARTEITARNUNG);
			}
		}
	}
}

