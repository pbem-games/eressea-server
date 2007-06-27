/* vi: set ts=2:
 *
 *
 *	Eressea PB(E)M host Copyright (C) 1998-2003
 *      Christian Schlittchen (corwin@amber.kn-bremen.de)
 *      Katja Zedel (katze@felidae.kn-bremen.de)
 *      Henning Peters (faroul@beyond.kn-bremen.de)
 *      Enno Rehling (enno@eressea-pbem.de)
 *      Ingo Wilken (Ingo.Wilken@informatik.uni-oldenburg.de)
 *
 *  based on:
 *
 * Atlantis v1.0  13 September 1993 Copyright 1993 by Russell Wallace
 * Atlantis v1.7                    Copyright 1996 by Alex Schr�der
 *
 * This program may not be used, modified or distributed without
 * prior permission by the authors of Eressea.
 * This program may not be sold or used commercially without prior written
 * permission from the authors.
 */

#define TEACH_ALL 1
#define TEACH_FRIENDS

#include <config.h>
#include "eressea.h"
#include "study.h"

#include <kernel/alchemy.h>
#include <kernel/building.h>
#include <kernel/faction.h>
#include <kernel/item.h>
#include <kernel/karma.h>
#include <kernel/magic.h>
#include <kernel/message.h>
#include <kernel/movement.h>
#include <kernel/order.h>
#include <kernel/plane.h>
#include <kernel/pool.h>
#include <kernel/race.h>
#include <kernel/region.h>
#include <kernel/skill.h>
#include <kernel/terrain.h>
#include <kernel/unit.h>

/* util includes */
#include <util/attrib.h>
#include <util/base36.h>
#include <util/parser.h>
#include <util/rand.h>
#include <util/umlaut.h>

/* libc includes */
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define TEACHNUMBER 10

static skill_t
getskill(const struct locale * lang)
{
	return findskill(getstrtoken(), lang);
}

magic_t
getmagicskill(const struct locale * lang)
{
  struct tnode * tokens = get_translations(lang, UT_MAGIC);
  variant token;
  const xmlChar * s = getstrtoken();

  if (findtoken(tokens, s, &token)==E_TOK_SUCCESS) {
    return (magic_t)token.i;
  }
  return M_NONE;
}

/* ------------------------------------------------------------- */
/* Vertraute und Kr�ten sind keine Migranten */
boolean
is_migrant(unit *u)
{
  if (u->race == u->faction->race) return false;

  if (fval(u->race, RCF_UNDEAD|RCF_ILLUSIONARY)) return false;
  if (is_familiar(u)) return false;
  if (u->race == new_race[RC_TOAD]) return false;

  return true;
}

/* ------------------------------------------------------------- */
boolean
magic_lowskill(unit *u)
{
	if (u->race == new_race[RC_TOAD]) return true;
	return false;
}

/* ------------------------------------------------------------- */

int
study_cost(unit *u, skill_t sk)
{
	int stufe, k = 50;

	switch (sk) {
		case SK_SPY:
			return 100;
			break;
		case SK_TACTICS:
		case SK_HERBALISM:
		case SK_ALCHEMY:
			return 200;
			break;
		case SK_MAGIC:	/* Die Magiekosten betragen 50+Summe(50*Stufe) */
				/* 'Stufe' ist dabei die n�chste zu erreichende Stufe */
			stufe = 1 + get_level(u, SK_MAGIC);
			return k*(1+((stufe+1)*stufe/2));
			break;
	}
	return 0;
}

/* ------------------------------------------------------------- */

static void 
init_learning(struct attrib * a)
{
	a->data.v = calloc(sizeof(teaching_info), 1);
}

static void 
done_learning(struct attrib * a)
{
	free(a->data.v);
}

const attrib_type at_learning = {
	"learning",
	init_learning, done_learning, NULL, NULL, NULL,
	ATF_UNIQUE
};

static int
teach_unit(unit * teacher, unit * student, int nteaching, skill_t sk, 
			  boolean report, int * academy)
{
  teaching_info * teach = NULL;
  attrib * a;
  int n;

  /* learning sind die Tage, die sie schon durch andere Lehrer zugute
  * geschrieben bekommen haben. Total darf dies nicht �ber 30 Tage pro Mann
  * steigen.
  *
  * n ist die Anzahl zus�tzlich gelernter Tage. n darf max. die Differenz
  * von schon gelernten Tagen zum max(30 Tage pro Mann) betragen. */

  if (magic_lowskill(student)){
    cmistake(teacher, teacher->thisorder, 292, MSG_EVENT);
    return 0;
  }

  n = student->number * 30;
  a = a_find(student->attribs, &at_learning);
  if (a!=NULL) {
    teach = (teaching_info*)a->data.v;
    n -= teach->value;
  }

  n = min(n, nteaching);

  if (n != 0) {
    struct building * b = inside_building(teacher);
    const struct building_type * btype = b?b->type:NULL;
    int index = 0;

    if (teach==NULL) {
      a = a_add(&student->attribs, a_new(&at_learning));
      teach = (teaching_info*)a->data.v;
    } else {
      while (teach->teachers[index] && index!=MAXTEACHERS) ++index;
    }
    if (index<MAXTEACHERS) teach->teachers[index++] = teacher;
    if (index<MAXTEACHERS) teach->teachers[index] = NULL;
    teach->value += n;

    /* Solange Akademien gr��enbeschr�nkt sind, sollte Lehrer und
    * Student auch in unterschiedlichen Geb�uden stehen d�rfen */
    if (btype == bt_find("academy")
      && student->building && student->building->type == bt_find("academy"))
    {
      int j = study_cost(student, sk);
      j = max(50, j * 2);
      /* kann Einheit das zahlen? */
      if (get_pooled(student, oldresourcetype[R_SILVER], GET_DEFAULT, j) >= j) {
        /* Jeder Sch�ler zus�tzlich +10 Tage wenn in Uni. */
        teach->value += (n / 30) * 10; /* learning erh�hen */
        /* Lehrer zus�tzlich +1 Tag pro Sch�ler. */
        if (academy) *academy += n;
      }	/* sonst nehmen sie nicht am Unterricht teil */
    }

    /* Teaching ist die Anzahl Leute, denen man noch was beibringen kann. Da
    * hier nicht n verwendet wird, werden die Leute gez�hlt und nicht die
    * effektiv gelernten Tage. -> FALSCH ? (ENNO)
    *
    * Eine Einheit A von 11 Mann mit Talent 0 profitiert vom ersten Lehrer B
    * also 10x30=300 tage, und der zweite Lehrer C lehrt f�r nur noch 1x30=30
    * Tage (damit das Maximum von 11x30=330 nicht �berschritten wird).
    *
    * Damit es aber in der Ausf�hrung nicht auf die Reihenfolge drauf ankommt,
    * darf der zweite Lehrer C keine weiteren Einheiten D mehr lehren. Also
    * wird student 30 Tage gutgeschrieben, aber teaching sinkt auf 0 (300-11x30 <=
    * 0).
    *
    * Sonst tr�te dies auf:
    *
    * A: lernt B: lehrt A C: lehrt A D D: lernt
    *
    * Wenn B vor C dran ist, lehrt C nur 30 Tage an A (wie oben) und
    * 270 Tage an D.
    *
    * Ist C aber vor B dran, lehrt C 300 tage an A, und 0 tage an D,
    * und B lehrt auch 0 tage an A.
    *
    * Deswegen darf C D nie lehren d�rfen.
    *
    * -> Das ist wirr. wer hat das entworfen?
    * Besser w�re, man macht erst vorab alle zuordnungen, und dann
    * die Talent�nderung (enno).
    */

    nteaching = max(0, nteaching - student->number * 30);

  }
  return n;
}

int
teach_cmd(unit * u, struct order * ord)
{
  static const curse_type * gbdream_ct = NULL;

  region * r = u->region;
  int teaching, i, j, count, academy=0;
  unit *u2;
  skill_t sk = NOSKILL;

  if (gbdream_ct==0) gbdream_ct = ct_find("gbdream");
  if (gbdream_ct) {
    if (get_curse(u->region->attribs, gbdream_ct)) {
      ADDMSG(&u->faction->msgs, 
        msg_feedback(u, ord, "gbdream_noteach", ""));
      return 0;
    }
  }

  if ((u->race->flags & RCF_NOTEACH) || fval(u, UFL_WERE)) {
    cmistake(u, ord, 274, MSG_EVENT);
    return 0;
  }

  if (r->planep && fval(r->planep, PFL_NOTEACH)) {
    cmistake(u, ord, 273, MSG_EVENT);
    return 0;
  }

  teaching = u->number * 30 * TEACHNUMBER;

  if ((i = get_effect(u, oldpotiontype[P_FOOL])) > 0) {	/* Trank "Dumpfbackenbrot" */
    i = min(i, u->number * TEACHNUMBER);
    /* Trank wirkt pro Sch�ler, nicht pro Lehrer */
    teaching -= i * 30;
    change_effect(u, oldpotiontype[P_FOOL], -i);
    j = teaching / 30;
    ADDMSG(&u->faction->msgs, msg_message("teachdumb",
      "teacher amount", u, j));
  }
  if (teaching == 0) return 0;


  u2 = 0;
  count = 0;

  init_tokens(ord);
  skip_token();

#if TEACH_ALL
  if (getparam(u->faction->locale)==P_ANY) {
    unit * student = r->units;
    skill_t teachskill[MAXSKILLS];
    int i = 0;
    do {
      sk = getskill(u->faction->locale);
      teachskill[i++]=sk;
    } while (sk!=NOSKILL);
    while (teaching && student) {
      if (student->faction == u->faction) {
#ifdef NEW_DAEMONHUNGER_RULE
        if (LongHunger(student)) continue;
#else
        if (fval(student, UFL_HUNGER)) continue;
#endif
        if (get_keyword(student->thisorder) == K_STUDY) {
          /* Input ist nun von student->thisorder !! */
          init_tokens(student->thisorder);
          skip_token();
          sk = getskill(student->faction->locale);
          if (sk!=NOSKILL && teachskill[0]!=NOSKILL) {
            for (i=0;teachskill[i]!=NOSKILL;++i) if (sk==teachskill[i]) break;
            sk = teachskill[i];
          }
          if (sk != NOSKILL && eff_skill_study(u, sk, r)-TEACHDIFFERENCE > eff_skill_study(student, sk, r)) {
            teaching -= teach_unit(u, student, teaching, sk, true, &academy);
          }
        }
      }
      student = student->next;
    }
#ifdef TEACH_FRIENDS
    while (teaching && student) {
      if (student->faction != u->faction && alliedunit(u, student->faction, HELP_GUARD)) {
#ifdef NEW_DAEMONHUNGER_RULE
        if (LongHunger(student)) continue;
#else
        if (fval(student, UFL_HUNGER)) continue;
#endif
        if (get_keyword(student->thisorder) == K_STUDY) {
          /* Input ist nun von student->thisorder !! */
          init_tokens(student->thisorder);
          skip_token();
          sk = getskill(student->faction->locale);
          if (sk != NOSKILL && eff_skill_study(u, sk, r)-TEACHDIFFERENCE >= eff_skill(student, sk, r)) {
            teaching -= teach_unit(u, student, teaching, sk, true, &academy);
          }
        }
      }
      student = student->next;
    }
#endif
  }
  else
#endif
  {
    char zOrder[4096];
    order * new_order;

    init_tokens(ord);
    skip_token();

    while (!parser_end()) {
      unit * u2 = getunit(r, u->faction);
      ++count;

      /* Falls die Unit nicht gefunden wird, Fehler melden */

      if (!u2) {
        xmlChar tbuf[20];
        const xmlChar * uid;
        const xmlChar * token;
        /* Finde den string, der den Fehler verursacht hat */
        parser_pushstate();
        init_tokens(ord);
        skip_token();

        for (j=0; j!=count-1; ++j) {
          /* skip over the first 'count' units */
          getunit(r, u->faction);
        }

        token = getstrtoken();

        /* Beginne die Fehlermeldung */
        if (findparam(token, u->faction->locale) != P_TEMP) {
          uid = token;
        } else {
          token = getstrtoken();
          sprintf((char*)tbuf, "%s %s", LOC(u->faction->locale,
            parameters[P_TEMP]), token);
          uid = tbuf;
        }
        ADDMSG(&u->faction->msgs, msg_feedback(u, ord, "unitnotfound_id",
          "id", uid));

        parser_popstate();
        continue;
      }

      /* Neuen Befehl zusammenbauen. TEMP-Einheiten werden automatisch in
       * ihre neuen Nummern �bersetzt. */
      strcat(zOrder, " ");
      strcat(zOrder, unitid(u2));

      if (get_keyword(u2->thisorder) != K_STUDY) {
        ADDMSG(&u->faction->msgs,
          msg_feedback(u, ord, "teach_nolearn", "student", u2));
        continue;
      }

      /* Input ist nun von u2->thisorder !! */
      parser_pushstate();
      init_tokens(u2->thisorder);
      skip_token();
      sk = getskill(u2->faction->locale);
      parser_popstate();

      if (sk == NOSKILL) {
        ADDMSG(&u->faction->msgs,
          msg_feedback(u, ord, "teach_nolearn", "student", u2));
        continue;
      }

      /* u is teacher, u2 is student */
      if (eff_skill_study(u2, sk, r) > eff_skill_study(u, sk, r)-TEACHDIFFERENCE) {
        ADDMSG(&u->faction->msgs,
          msg_feedback(u, ord, "teach_asgood", "student", u2));
        continue;
      }
      if (sk == SK_MAGIC) {
        /* ist der Magier schon spezialisiert, so versteht er nur noch
        * Lehrer seines Gebietes */
        if (find_magetype(u2) != 0
          && find_magetype(u) != find_magetype(u2))
        {
          ADDMSG(&u->faction->msgs, msg_feedback(u, ord, "error_different_magic", "target", u2));
          continue;
        }
      }

      teaching -= teach_unit(u, u2, teaching, sk, false, &academy);
    }
    new_order = create_order(K_TEACH, u->faction->locale, "%s", zOrder);
#ifdef LASTORDER
    set_order(&u->lastorder, new_order);
#else
    replace_order(&u->orders, ord, new_order);
    free_order(new_order); /* parse_order & set_order have each increased the refcount */
#endif
  }
  if (academy && sk!=NOSKILL) {
    academy = academy/30; /* anzahl gelehrter wochen, max. 10 */
    learn_skill(u, sk, academy/30.0/TEACHNUMBER);
  }
  return 0;
}
/* ------------------------------------------------------------- */

int
learn_cmd(unit * u, order * ord)
{
  region *r = u->region;
  int p;
  magic_t mtyp;
  int l;
  int studycost, days;
  double multi = 1.0;
  attrib * a = NULL;
  teaching_info * teach = NULL;
  int money = 0;
  skill_t sk;
  int maxalchemy = 0;
  static int learn_newskills = -1;
  if (learn_newskills<0) {
    const char * str = get_param(global.parameters, "study.newskills");
    if (str && strcmp(str, "false")==0) learn_newskills = 0;
    else learn_newskills = 1;
  }

  if ((u->race->flags & RCF_NOLEARN) || fval(u, UFL_WERE)) {
    ADDMSG(&u->faction->msgs, msg_feedback(u, ord, "error_race_nolearn", "race", u->race));
    return 0;
  }

  init_tokens(ord);
  skip_token();
  sk = getskill(u->faction->locale);

  if (sk < 0) {
    cmistake(u, ord, 77, MSG_EVENT);
    return 0;
  }
  if (SkillCap(sk) && SkillCap(sk) <= effskill(u, sk)) {
	  cmistake(u, ord, 77, MSG_EVENT);
    return 0;
  }
  /* Hack: Talente mit Malus -99 k�nnen nicht gelernt werden */
  if (u->race->bonus[sk] == -99) {
    cmistake(u, ord, 77, MSG_EVENT);
    return 0;
  }
  if (learn_newskills==0) {
    skill * sv = get_skill(u, sk);
    if (sv==NULL) {
      /* we can only learn skills we already have */
      cmistake(u, ord, 77, MSG_EVENT);
      return 0;
    }
  }

  /* snotlings k�nnen Talente nur bis T8 lernen */
  if (u->race == new_race[RC_SNOTLING]){
    if (get_level(u, sk) >= 8){
      cmistake(u, ord, 308, MSG_EVENT);
      return 0;
    }
  }

  p = studycost = study_cost(u, sk);
  a = a_find(u->attribs, &at_learning);
  if (a!=NULL) {
    teach = (teaching_info*)a->data.v;
  }

  /* keine kostenpflichtigen Talente f�r Migranten. Vertraute sind
  * keine Migranten, wird in is_migrant abgefangen. Vorsicht,
  * studycost darf hier noch nicht durch Akademie erh�ht sein */
  if (studycost > 0 && !ExpensiveMigrants() && is_migrant(u)) {
    ADDMSG(&u->faction->msgs, msg_feedback(u, ord, "error_migrants_nolearn", ""));
    return 0;
  }
  /* Akademie: */
  {
    struct building * b = inside_building(u);
    const struct building_type * btype = b?b->type:NULL;

    if (btype == bt_find("academy")) {
      studycost = max(50, studycost * 2);
    }
  }

  if (sk == SK_MAGIC) {
    if (u->number > 1){
      cmistake(u, ord, 106, MSG_MAGIC);
      return 0;
    }
    if (is_familiar(u)){
      /* Vertraute z�hlen nicht zu den Magiern einer Partei,
      * k�nnen aber nur Graue Magie lernen */
      mtyp = M_GRAU;
      if (!is_mage(u)) create_mage(u, mtyp);
    } else if (!has_skill(u, SK_MAGIC)) {
      int mmax = max_skill(u->faction, SK_MAGIC);
      /* Die Einheit ist noch kein Magier */
      if (count_skill(u->faction, SK_MAGIC) + u->number > mmax)
      {
        ADDMSG(&u->faction->msgs, msg_feedback(u, ord, "error_max_magicians", "amount", mmax));
        return 0;
      }
      mtyp = getmagicskill(u->faction->locale);
      if (mtyp == M_NONE || mtyp == M_GRAU) {
        /* wurde kein Magiegebiet angegeben, wird davon
        * ausgegangen, da� das normal gelernt werden soll */
        if(u->faction->magiegebiet != 0) {
          mtyp = u->faction->magiegebiet;
        } else {
          /* Es wurde kein Magiegebiet angegeben und die Partei
          * hat noch keins gew�hlt. */
          cmistake(u, ord, 178, MSG_MAGIC);
          return 0;
        }
      }
      if (mtyp != u->faction->magiegebiet){
        /* Es wurde versucht, ein anderes Magiegebiet zu lernen
        * als das der Partei */
        if (u->faction->magiegebiet != 0){
          cmistake(u, ord, 179, MSG_MAGIC);
          return 0;
        } else {
          /* Lernt zum ersten mal Magie und legt damit das
          * Magiegebiet der Partei fest */
          u->faction->magiegebiet = mtyp;
        }
      }
      if (!is_mage(u)) create_mage(u, mtyp);
    } else {
      /* ist schon ein Magier und kein Vertrauter */
      if(u->faction->magiegebiet == 0){
        /* die Partei hat noch kein Magiegebiet gew�hlt. */
        mtyp = getmagicskill(u->faction->locale);
        if (mtyp == M_NONE){
          cmistake(u, ord, 178, MSG_MAGIC);
          return 0;
        } else {
          /* Legt damit das Magiegebiet der Partei fest */
          u->faction->magiegebiet = mtyp;
        }
      }
    }
  }
  if (sk == SK_ALCHEMY) {
    maxalchemy = eff_skill(u, SK_ALCHEMY, r);
    if (!has_skill(u, SK_ALCHEMY)) {
      int amax = max_skill(u->faction, SK_ALCHEMY);
      if (count_skill(u->faction, SK_ALCHEMY) + u->number > amax) {
        ADDMSG(&u->faction->msgs, msg_feedback(u, ord, "error_max_alchemists", "amount", amax));
        return 0;
      }
    }
  }
  if (studycost) {
    int cost = studycost * u->number;
    money = get_pooled(u, oldresourcetype[R_SILVER], GET_DEFAULT, cost);
    money = min(money, cost);
  }
  if (money < studycost * u->number) {
    studycost = p;	/* Ohne Univertreurung */
    money = min(money, studycost);
    if (p>0 && money < studycost * u->number) {
      cmistake(u, ord, 65, MSG_EVENT);
      multi = money / (double)(studycost * u->number);
    }
  }

  if (teach==NULL) {
    a = a_add(&u->attribs, a_new(&at_learning));
    teach = (teaching_info*)a->data.v;
    teach->teachers[0] = 0;
  }
  if (money>0) {
    use_pooled(u, oldresourcetype[R_SILVER], GET_DEFAULT, money);
    ADDMSG(&u->faction->msgs, msg_message("studycost",
      "unit region cost skill", u, u->region, money, sk));
  }

  if (get_effect(u, oldpotiontype[P_WISE])) {
    l = min(u->number, get_effect(u, oldpotiontype[P_WISE]));
    teach->value += l * 10;
    change_effect(u, oldpotiontype[P_WISE], -l);
  }
  if (get_effect(u, oldpotiontype[P_FOOL])) {
    l = min(u->number, get_effect(u, oldpotiontype[P_FOOL]));
    teach->value -= l * 30;
    change_effect(u, oldpotiontype[P_FOOL], -l);
  }

  #ifdef KARMA_MODULE
  l = fspecial(u->faction, FS_WARRIOR);
  if (l > 0) {
    if (sk == SK_CROSSBOW || sk == SK_LONGBOW
      || sk == SK_CATAPULT || sk == SK_MELEE || sk == SK_SPEAR
      || sk == SK_AUSDAUER || sk == SK_WEAPONLESS)
    {
      teach->value += u->number * 5 * (l+1);
    } else {
      teach->value -= u->number * 5 * (l+1);
      teach->value = max(0, teach->value);
    }
  }
  #endif /* KARMA_MODULE */

  if (p != studycost) {
    /* ist_in_gebaeude(r, u, BT_UNIVERSITAET) == 1) { */
    /* p ist Kosten ohne Uni, studycost mit; wenn
    * p!=studycost, ist die Einheit zwangsweise
    * in einer Uni */
    teach->value += u->number * 10;
  }

  if (is_cursed(r->attribs, C_BADLEARN,0)) {
    teach->value -= u->number * 10;
  }

  days = (int)((u->number * 30 + teach->value) * multi);

  /* the artacademy currently improves the learning of entertainment
  of all units in the region, to be able to make it cumulative with
  with an academy */

  if (sk == SK_ENTERTAINMENT && buildingtype_exists(r, bt_find("artacademy"))) {
    days *= 2;
  }

  if (fval(u, UFL_HUNGER)) days /= 2;

  while (days) {
    if (days>=u->number*30) {
      learn_skill(u, sk, 1.0);
      days -= u->number*30;
    } else {
      double chance = (double)days/u->number/30;
      learn_skill(u, sk, chance);
      days = 0;
    }
  }
  if (a!=NULL) {
    if (teach!=NULL) {
      int index = 0;
      while (teach->teachers[index] && index!=MAXTEACHERS) {
        unit * teacher = teach->teachers[index++];
        if (teacher->faction != u->faction) {
          ADDMSG(&u->faction->msgs, msg_message("teach_student",
            "teacher student skill", teacher, u, sk));
          ADDMSG(&teacher->faction->msgs, msg_message("teach_teacher",
            "teacher student skill level", teacher, u, sk,
            effskill(u, sk)));
        }
      }
    }
    a_remove(&u->attribs, a);
    a = NULL;
  }
  fset(u, UFL_LONGACTION|UFL_NOTMOVING);

  /* Anzeigen neuer Tr�nke */
  /* Spruchlistenaktualiesierung ist in Regeneration */

  if (sk == SK_ALCHEMY) {
    const potion_type * ptype;
    faction * f = u->faction;
    int skill = eff_skill(u, SK_ALCHEMY, r);
    if (skill>maxalchemy) {
      for (ptype=potiontypes; ptype; ptype=ptype->next) {
        if (skill == ptype->level * 2) {
          attrib * a = a_find(f->attribs, &at_showitem);
          while (a && a->type==&at_showitem && a->data.v != ptype) a=a->next;
          if (a==NULL || a->type!=&at_showitem) {
            a = a_add(&f->attribs, a_new(&at_showitem));
            a->data.v = (void*) ptype->itype;
          }
        }
      }
    }
  }
  return 0;
}
