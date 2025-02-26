#include "study.h"

#include "magic.h"            // for create_mage, get_mage, unit_get_magic

#include "kernel/ally.h"
#include "kernel/attrib.h"
#include "kernel/config.h"
#include "kernel/building.h"
#include "kernel/faction.h"
#include "kernel/item.h"
#include "kernel/order.h"
#include "kernel/race.h"
#include "kernel/region.h"
#include "kernel/skill.h"     // for SK_CROSSBOW, SK_ALCHEMY, SK_MAGIC, skil...
#include "kernel/skills.h"
#include "kernel/types.h"     // for M_GWYRRD
#include "kernel/unit.h"

#include "util/base36.h"
#include "util/keyword.h"     // for K_STUDY, K_TEACH
#include "util/language.h"
#include "util/message.h"
#include "util/rand.h"
#include "util/variant.h"     // for variant

#include <tests.h>

#include <CuTest.h>
#include <selist.h>

#include <assert.h>
#include <stddef.h>           // for NULL

struct locale;

#define MAXLOG 4
typedef struct log_entry {
    unit *u;
    skill_t sk;
    int days;
} log_entry;

static log_entry log_learners[MAXLOG];
static int log_size;

static void log_learn(unit *u, skill_t sk, int days) {
    if (log_size < MAXLOG) {
        log_entry * entry = &log_learners[log_size++];
        entry->u = u;
        entry->sk = sk;
        entry->days = days;
    }
}

void learn_inject(void) {
    log_size = 0;
    inject_learn(log_learn);
}

void learn_reset(void) {
    inject_learn(0);
}

typedef struct {
    unit *u;
    unit *teachers[2];
} study_fixture;

static void setup_study(void) {
    test_setup();
    mt_create_error(77);
    mt_create_error(771);
    mt_create_error(178);
    mt_create_error(65);
    mt_create_error(274);
    mt_create_va(mt_new("teach_asgood", NULL),
        "unit:unit", "region:region", "command:order", "student:unit", MT_NEW_END);
    mt_create_va(mt_new("studycost", NULL),
        "unit:unit", "region:region", "cost:int", "skill:int", MT_NEW_END);
    mt_create_va(mt_new("teach_teacher", NULL),
        "teacher:unit", "student:unit", "skill:int", "level:int", MT_NEW_END);
    mt_create_va(mt_new("teach_student", NULL),
        "teacher:unit", "student:unit", "skill:int", MT_NEW_END);
}

static void setup_locale(struct locale *lang) {
    int i;
    for (i = 0; i < MAXSKILLS; ++i) {
        if (!locale_getstring(lang, mkname("skill", skillnames[i])))
            locale_setstring(lang, mkname("skill", skillnames[i]), skillnames[i]);
    }
    init_skills(lang);
}

static void setup_teacher(study_fixture *fix, skill_t sk) {
    struct region * r;
    struct faction *f;
    struct locale *lang;

    assert(fix);
    setup_study();
    config_set("study.random_progress", "0");
    r = test_create_plain(0, 0);
    f = test_create_faction();
    f->locale = lang = test_create_locale();
    setup_locale(lang);
    fix->u = test_create_unit(f, r);
    assert(fix->u);
    fix->u->thisorder = create_order(K_STUDY, f->locale, skillnames[sk]);

    fix->teachers[0] = test_create_unit(f, r);
    assert(fix->teachers[0]);
    fix->teachers[0]->thisorder = create_order(K_TEACH, f->locale, itoa36(fix->u->no));

    fix->teachers[1] = test_create_unit(f, r);
    assert(fix->teachers[1]);
    fix->teachers[1]->thisorder = create_order(K_TEACH, f->locale, itoa36(fix->u->no));
    test_clear_messages(f);
}

static void test_study_no_teacher(CuTest *tc) {
    study_fixture fix;
    skill *sv;

    setup_teacher(&fix, SK_CROSSBOW);
    study_cmd(fix.u, fix.u->thisorder);
    sv = unit_skill(fix.u, SK_CROSSBOW);
    CuAssertPtrNotNull(tc, sv);
    CuAssertIntEquals(tc, 1, sv->level);
    CuAssertIntEquals(tc, 2 * SKILL_DAYS_PER_WEEK, sv->days);
    CuAssertPtrEquals(tc, NULL, test_get_last_message(fix.u->faction->msgs));
    test_teardown();
}

static void test_study_with_teacher(CuTest *tc) {
    study_fixture fix;
    skill *sv;

    setup_teacher(&fix, SK_CROSSBOW);
    set_level(fix.teachers[0], SK_CROSSBOW, TEACHDIFFERENCE);
    teach_cmd(fix.teachers[0], fix.teachers[0]->thisorder);
    CuAssertPtrEquals(tc, NULL, test_get_last_message(fix.u->faction->msgs));
    study_cmd(fix.u, fix.u->thisorder);
    CuAssertPtrNotNull(tc, sv = unit_skill(fix.u, SK_CROSSBOW));
    CuAssertIntEquals(tc, 1, sv->level);
    CuAssertIntEquals(tc, 1 * SKILL_DAYS_PER_WEEK, sv->days);
    test_teardown();
}

static void test_study_with_bad_teacher(CuTest *tc) {
    study_fixture fix;
    skill *sv;

    setup_teacher(&fix, SK_CROSSBOW);
    teach_cmd(fix.teachers[0], fix.teachers[0]->thisorder);
    CuAssertPtrNotNull(tc, test_find_messagetype(fix.u->faction->msgs, "teach_asgood"));
    study_cmd(fix.u, fix.u->thisorder);
    CuAssertPtrNotNull(tc, sv = unit_skill(fix.u, SK_CROSSBOW));
    CuAssertIntEquals(tc, 1, sv->level);
    CuAssertIntEquals(tc, 2 * SKILL_DAYS_PER_WEEK, sv->days);
    test_teardown();
}

static void test_study_race_noteach(CuTest *tc) {
    study_fixture fix;
    skill *sv;
    race* rc;

    setup_teacher(&fix, SK_CROSSBOW);
    rc = test_create_race("tunnelworm");
    rc->flags |= RCF_NOTEACH;
    u_setrace(fix.teachers[0], rc);
    CuAssertTrue(tc, !can_teach(fix.teachers[0]));
    teach_cmd(fix.teachers[0], fix.teachers[0]->thisorder);
    CuAssertPtrNotNull(tc, test_find_messagetype(fix.u->faction->msgs, "error274"));
    study_cmd(fix.u, fix.u->thisorder);
    CuAssertPtrNotNull(tc, sv = unit_skill(fix.u, SK_CROSSBOW));
    CuAssertIntEquals(tc, 1, sv->level);
    CuAssertIntEquals(tc, 2 * SKILL_DAYS_PER_WEEK, sv->days);
    test_teardown();
}

static void test_study_speed(CuTest *tc) {
    unit *u;
    race *rc;
    skill *sv;

    test_setup();
    learn_inject();
    rc = test_create_race("orc");
    u = test_create_unit(test_create_faction_ex(rc, NULL), test_create_plain(0, 0));
    set_level(u, SK_BUILDING, 1);
    set_level(u, SK_CATAPULT, 1);
    set_study_speed(rc, SK_BUILDING, -5);
    sv = unit_skill(u, SK_BUILDING);
    sv->days = 1 * SKILL_DAYS_PER_WEEK;
    CuAssertIntEquals(tc, -5, rc->study_speed[SK_BUILDING]);
    u->thisorder = create_order(K_STUDY, u->faction->locale, skillnames[SK_BUILDING]);
    random_source_inject_constants(0.0, 0);
    study_cmd(u, u->thisorder);
    CuAssertPtrEquals(tc, u, log_learners[0].u);
    CuAssertIntEquals(tc, SK_BUILDING, log_learners[0].sk);
    CuAssertIntEquals(tc, 25, log_learners[0].days);
    CuAssertIntEquals(tc, 1, sv->level);
    CuAssertIntEquals(tc, 1 * SKILL_DAYS_PER_WEEK, sv->days);

    random_source_inject_constants(0.0, 5);
    u->flags &= ~UFL_LONGACTION;
    study_cmd(u, u->thisorder);
    CuAssertPtrEquals(tc, u, log_learners[1].u);
    CuAssertIntEquals(tc, SK_BUILDING, log_learners[1].sk);
    CuAssertIntEquals(tc, 25, log_learners[1].days);
    CuAssertIntEquals(tc, 2, sv->level);

    free_order(u->thisorder);
    u->thisorder = create_order(K_STUDY, u->faction->locale, skillnames[SK_CATAPULT]);
    u->flags &= ~UFL_LONGACTION;
    study_cmd(u, u->thisorder);
    CuAssertPtrEquals(tc, u, log_learners[2].u);
    CuAssertIntEquals(tc, SK_CATAPULT, log_learners[2].sk);
    CuAssertIntEquals(tc, 30, log_learners[2].days);

    learn_reset();
    test_teardown();
}

static void test_check_student(CuTest *tc) {
    unit *u;
    race *rc;

    setup_study();
    u = test_create_unit(test_create_faction(), test_create_plain(0, 0));
    u->thisorder = create_order(K_STUDY, u->faction->locale, skillnames[SK_CROSSBOW]);
    CuAssertTrue(tc, check_student(u, u->thisorder, SK_CROSSBOW));
    CuAssertPtrEquals(tc, NULL, u->faction->msgs);

    rc = test_create_race("skeleton");
    rc->flags |= RCF_NOLEARN;
    u_setrace(u, rc);
    CuAssertTrue(tc, !check_student(u, u->thisorder, SK_CROSSBOW));
    CuAssertPtrNotNull(tc, test_find_messagetype(u->faction->msgs, "error_race_nolearn"));
    test_clear_messages(u->faction);
    rc->flags -= RCF_NOLEARN;

    rc->bonus[SK_CROSSBOW] = -99;
    CuAssertTrue(tc, !check_student(u, u->thisorder, SK_CROSSBOW));
    CuAssertPtrNotNull(tc, test_find_messagetype(u->faction->msgs, "error771"));
    test_clear_messages(u->faction);

    test_teardown();
}

static void test_study_bug_2194(CuTest *tc) {
    unit *u, *u1, *u2;
    struct locale * loc;
    building * b;

    setup_study();
    random_source_inject_constant(0.0);
    init_resources();
    loc = test_create_locale();
    setup_locale(loc);
    u = test_create_unit(test_create_faction(), test_create_plain(0, 0));
    scale_number(u, 2);
    set_level(u, SK_CROSSBOW, TEACHDIFFERENCE);
    u->faction->locale = loc;
    u1 = test_create_unit(u->faction, u->region);
    scale_number(u1, 17);
    u1->thisorder = create_order(K_STUDY, loc, skillnames[SK_CROSSBOW]);
    u2 = test_create_unit(u->faction, u->region);
    scale_number(u2, 3);
    u2->thisorder = create_order(K_STUDY, loc, skillnames[SK_MAGIC]);
    u->thisorder = create_order(K_TEACH, loc, "%s %s", itoa36(u1->no), itoa36(u2->no));
    b = test_create_building(u->region, test_create_buildingtype("academy"));
    b->size = 22;
    u_set_building(u, b);
    u_set_building(u1, b);
    u_set_building(u2, b);
    i_change(&u1->items, get_resourcetype(R_SILVER)->itype, 50);
    i_change(&u2->items, get_resourcetype(R_SILVER)->itype, 50);
    learn_inject();
    teach_cmd(u, u->thisorder);
    learn_reset();
    CuAssertPtrEquals(tc, u, log_learners[0].u);
    CuAssertIntEquals(tc, SK_CROSSBOW, log_learners[0].sk);
    CuAssertIntEquals(tc, 1, log_size);
    CuAssertPtrNotNull(tc, test_find_messagetype(u->faction->msgs, "teach_asgood"));

    free_order(u->thisorder);
    u->thisorder = create_order(K_TEACH, loc, itoa36(u2->no));
    learn_inject();
    teach_cmd(u, u->thisorder);
    learn_reset();
    CuAssertIntEquals(tc, 0, log_size);
    test_teardown();
}

static void test_academy_building(CuTest *tc) {
    unit *u, *u1, *u2;
    struct locale * loc;
    building * b;
    message * msg;

    setup_study();
    mt_create_va(mt_new("teach_asgood", NULL),
        "unit:unit", "region:region", "command:order", "student:unit", MT_NEW_END);

    random_source_inject_constant(0.0);
    init_resources();
    loc = test_create_locale();
    setup_locale(loc);
    u = test_create_unit(test_create_faction(), test_create_plain(0, 0));
    scale_number(u, 2);
    set_level(u, SK_CROSSBOW, TEACHDIFFERENCE);
    u->faction->locale = loc;
    u1 = test_create_unit(u->faction, u->region);
    scale_number(u1, 15);
    u1->thisorder = create_order(K_STUDY, loc, skillnames[SK_CROSSBOW]);
    u2 = test_create_unit(u->faction, u->region);
    scale_number(u2, 5);
    u2->thisorder = create_order(K_STUDY, loc, skillnames[SK_CROSSBOW]);
    set_level(u2, SK_CROSSBOW, 1);
    u->thisorder = create_order(K_TEACH, loc, "%s %s", itoa36(u1->no), itoa36(u2->no));
    b = test_create_building(u->region, test_create_buildingtype("academy"));
    b->size = 22;
    u_set_building(u, b);
    u_set_building(u1, b);
    u_set_building(u2, b);
    i_change(&u1->items, get_resourcetype(R_SILVER)->itype, 50);
    i_change(&u2->items, get_resourcetype(R_SILVER)->itype, 50);
    learn_inject();
    teach_cmd(u, u->thisorder);
    learn_reset();
    CuAssertPtrNotNull(tc, msg = test_find_messagetype(u->faction->msgs, "teach_asgood"));
    CuAssertPtrEquals(tc, u, (unit *)msg->parameters[0].v);
    CuAssertPtrEquals(tc, u2, (unit *)msg->parameters[3].v);

    CuAssertPtrEquals(tc, u, log_learners[0].u);
    CuAssertIntEquals(tc, SK_CROSSBOW, log_learners[0].sk);
    CuAssertIntEquals(tc, u1->number, log_learners[0].days);
    test_teardown();
}

/*
u0 (1) TEACH u3 (1) u1 (9/10)
u (2) TEACH u1 (1/10)
 */

static void test_academy_bonus(CuTest *tc) {
    unit *u, *u0, *u1, *u3;
    struct locale * loc;
    building * b;

    setup_study();

    random_source_inject_constant(0.0);
    init_resources();
    loc = test_create_locale();
    setup_locale(loc);
    u = test_create_unit(test_create_faction(), test_create_plain(0, 0));
    u->faction->locale = loc;

    u0 = test_create_unit(test_create_faction(), test_create_plain(0, 0));
    set_level(u, SK_CROSSBOW, TEACHDIFFERENCE);
    set_level(u0, SK_CROSSBOW, TEACHDIFFERENCE);
    
    u1 = test_create_unit(u->faction, u->region);
    u3 = test_create_unit(u->faction, u->region);
    u0->thisorder = create_order(K_TEACH, loc, "%s %s", itoa36(u3->no), itoa36(u1->no));
    u->thisorder = create_order(K_TEACH, loc, itoa36(u1->no));
    u1->thisorder = create_order(K_STUDY, loc, skillnames[SK_CROSSBOW]);
    u3->thisorder = create_order(K_STUDY, loc, skillnames[SK_CROSSBOW]);
    
    b = test_create_building(u->region, test_create_buildingtype("academy"));
    b->size = 25;
    u_set_building(u, b);
    u_set_building(u0, b);
    u_set_building(u1, b);
    u_set_building(u3, b);

    scale_number(u, 2);
    scale_number(u1, 9);
    scale_number(u3, 2);
    i_change(&u1->items, get_resourcetype(R_SILVER)->itype, 5000);

    learn_inject();
    teach_cmd(u0, u0->thisorder);
    teach_cmd(u, u->thisorder);
    study_cmd(u1, u1->thisorder);
    study_cmd(u3, u3->thisorder);

    CuAssertIntEquals(tc, 4, log_size);
    CuAssertIntEquals(tc, SK_CROSSBOW, log_learners[0].sk);
    CuAssertPtrEquals(tc, u0, log_learners[0].u);
    CuAssertIntEquals(tc, 10, log_learners[0].days);
    CuAssertPtrEquals(tc, u, log_learners[1].u);
    CuAssertIntEquals(tc, 1, log_learners[1].days);
    CuAssertPtrEquals(tc, u1, log_learners[2].u);
    CuAssertIntEquals(tc, 720, log_learners[2].days);
    CuAssertPtrEquals(tc, u3, log_learners[3].u);
    CuAssertIntEquals(tc, 160, log_learners[3].days);
    learn_reset();
    test_teardown();
}

void test_learn_skill_single(CuTest *tc) {
    unit *u;
    skill *sv;

    setup_study();
    config_set("study.random_progress", "0");
    u = test_create_unit(test_create_faction(), test_create_plain(0, 0));
    CuAssertIntEquals(tc, 0, learn_skill(u, SK_ALCHEMY, SKILL_DAYS_PER_WEEK, 0));
    CuAssertPtrNotNull(tc, sv = u->skills);
    CuAssertIntEquals(tc, SK_ALCHEMY, sv->id);
    CuAssertIntEquals(tc, 1, sv->level);
    CuAssertIntEquals(tc, 2 * SKILL_DAYS_PER_WEEK, sv->days);
    CuAssertIntEquals(tc, 0, learn_skill(u, SK_ALCHEMY, SKILL_DAYS_PER_WEEK, 0));
    CuAssertIntEquals(tc, 1 * SKILL_DAYS_PER_WEEK, sv->days);
    CuAssertIntEquals(tc, 0, learn_skill(u, SK_ALCHEMY, SKILL_DAYS_PER_WEEK * 2, 0));
    CuAssertIntEquals(tc, 2, sv->level);
    CuAssertIntEquals(tc, 2 * SKILL_DAYS_PER_WEEK, sv->days);
    test_teardown();
}

void test_learn_skill_multi(CuTest *tc) {
    unit *u;
    skill *sv;

    setup_study();
    config_set("study.random_progress", "0");
    u = test_create_unit(test_create_faction(), test_create_plain(0, 0));
    scale_number(u, 10);
    CuAssertIntEquals(tc, 0, learn_skill(u, SK_ALCHEMY, SKILL_DAYS_PER_WEEK * u->number, 0));
    CuAssertPtrNotNull(tc, sv = u->skills);
    CuAssertIntEquals(tc, SK_ALCHEMY, sv->id);
    CuAssertIntEquals(tc, 1, sv->level);
    CuAssertIntEquals(tc, 2 * SKILL_DAYS_PER_WEEK, sv->days);
    CuAssertIntEquals(tc, 0, learn_skill(u, SK_ALCHEMY, SKILL_DAYS_PER_WEEK * u->number, 0));
    CuAssertIntEquals(tc, 1 * SKILL_DAYS_PER_WEEK, sv->days);
    CuAssertIntEquals(tc, 0, learn_skill(u, SK_ALCHEMY, SKILL_DAYS_PER_WEEK * u->number * 2, 0));
    CuAssertIntEquals(tc, 2, sv->level);
    CuAssertIntEquals(tc, 2 * SKILL_DAYS_PER_WEEK, sv->days);
    test_teardown();
}

static void test_demon_skillchange(CuTest *tc) {
    unit * u;
    const race * rc;

    setup_study();
    rc = test_create_race("demon");
    CuAssertPtrEquals(tc, (void *)rc, (void *)get_race(RC_DAEMON));
    u = test_create_unit(test_create_faction_ex(rc, NULL), test_create_plain(0, 0));
    CuAssertPtrNotNull(tc, u);
    test_set_skill(u, SK_CROSSBOW, 2, 1);
    test_set_skill(u, SK_MELEE, 2, 1);
    CuAssertIntEquals(tc, 1 * SKILL_DAYS_PER_WEEK, skill_days(u, SK_CROSSBOW));
    CuAssertIntEquals(tc, 2, skill_level(u, SK_CROSSBOW));

    // make all changes add or subtract only one week:
    config_set_int("skillchange.demon.max", 1);

    /* feature disabled */
    config_set_int("skillchange.demon.down", 0);
    config_set_int("skillchange.demon.up", 0);
    demon_skillchange(u);
    CuAssertIntEquals(tc, 1 * SKILL_DAYS_PER_WEEK, skill_days(u, SK_CROSSBOW));
    CuAssertIntEquals(tc, 2, skill_level(u, SK_CROSSBOW));

    /* 10 % chance of rise / fall */
    config_set_int("skillchange.demon.down", 10);
    config_set_int("skillchange.demon.up", 10);
    // roll a loss:
    random_source_inject_constants(0.f, 0);
    demon_skillchange(u);
    CuAssertIntEquals(tc, 2 * SKILL_DAYS_PER_WEEK, skill_days(u, SK_CROSSBOW));
    CuAssertIntEquals(tc, 2 * SKILL_DAYS_PER_WEEK, skill_days(u, SK_MELEE));
    // roll a gain:
    random_source_inject_constants(0.f, 10);
    demon_skillchange(u);
    CuAssertIntEquals(tc, 1 * SKILL_DAYS_PER_WEEK, skill_days(u, SK_CROSSBOW));
    // roll no change:
    random_source_inject_constants(0.f, 20);
    demon_skillchange(u);
    CuAssertIntEquals(tc, 1 * SKILL_DAYS_PER_WEEK, skill_days(u, SK_CROSSBOW));
    // sanity check:
    CuAssertIntEquals(tc, 2, skill_level(u, SK_CROSSBOW));

    test_teardown();
}

static void test_demon_skillchange_hungry(CuTest *tc) {
    unit * u;
    const race * rc;

    setup_study();
    rc = test_create_race("demon");
    CuAssertPtrEquals(tc, (void *)rc, (void *)get_race(RC_DAEMON));
    u = test_create_unit(test_create_faction_ex(rc, NULL), test_create_plain(0, 0));
    fset(u, UFL_HUNGER);
    test_set_skill(u, SK_CROSSBOW, 2, 1);
    CuAssertIntEquals(tc, 1 * SKILL_DAYS_PER_WEEK, skill_days(u, SK_CROSSBOW));
    CuAssertIntEquals(tc, 2, skill_level(u, SK_CROSSBOW));

    // make all changes add or subtract only one week:
    config_set_int("skillchange.demon.max", 1);

    /* hungry units will only go down, in 20 % of cases(the "up" chance) */
    config_set_int("hunger.demon.skills", 1);
    config_set_int("skillchange.demon.up", 20);
    // this value is ignored/replaced:
    config_set_int("skillchange.demon.down", 10);

    // roll a normal loss:
    random_source_inject_constants(0.f, 0);
    demon_skillchange(u);
    CuAssertIntEquals(tc, 2 * SKILL_DAYS_PER_WEEK, skill_days(u, SK_CROSSBOW));
    // this should be a success, is also still a loss:
    random_source_inject_constants(0.f, 10);
    demon_skillchange(u);
    CuAssertIntEquals(tc, 3 * SKILL_DAYS_PER_WEEK, skill_days(u, SK_CROSSBOW));
    // no positive change:
    random_source_inject_constants(0.f, 20);
    demon_skillchange(u);
    CuAssertIntEquals(tc, 3 * SKILL_DAYS_PER_WEEK, skill_days(u, SK_CROSSBOW));
    // sanity check:
    CuAssertIntEquals(tc, 2, skill_level(u, SK_CROSSBOW));

    test_teardown();
}

static void test_study_cmd(CuTest *tc) {
    unit *u;

    setup_study();
    init_resources();
    u = test_create_unit(test_create_faction(), test_create_plain(0, 0));
    u->thisorder = create_order(K_STUDY, u->faction->locale, "CROSSBOW");
    learn_inject();
    study_cmd(u, u->thisorder);
    learn_reset();
    CuAssertPtrEquals(tc, u, log_learners[0].u);
    CuAssertIntEquals(tc, SK_CROSSBOW, log_learners[0].sk);
    CuAssertIntEquals(tc, SKILL_DAYS_PER_WEEK, log_learners[0].days);
    test_teardown();
}

static void test_study_magic(CuTest *tc) {
    unit *u;
    faction *f;
    const struct locale *lang;
    const struct item_type *itype;

    setup_study();
    init_resources();
    f = test_create_faction();
    lang = f->locale;
    u = test_create_unit(f, test_create_plain(0, 0));
    u->thisorder = create_order(K_STUDY, lang, skillnames[SK_MAGIC]);
    itype = test_create_silver();

    CuAssertIntEquals(tc, -1, study_cmd(u, u->thisorder));
    CuAssertPtrNotNull(tc, test_find_messagetype(f->msgs, "error178"));
    free_order(u->thisorder);

    test_clear_messages(f);
    u->thisorder = create_order(K_STUDY, lang, "%s %s", skillnames[SK_MAGIC], magic_school[M_GWYRRD]);
    CuAssertIntEquals(tc, 0, study_cmd(u, u->thisorder));
    CuAssertPtrNotNull(tc, test_find_messagetype(f->msgs, "error65"));

    test_clear_messages(f);
    i_change(&u->items, itype, 100);
    CuAssertIntEquals(tc, 0, study_cmd(u, u->thisorder));
    CuAssertIntEquals(tc, M_GWYRRD, f->magiegebiet);
    CuAssertIntEquals(tc, 0, i_get(u->items, itype));
    CuAssertPtrNotNull(tc, get_mage(u));
    CuAssertPtrEquals(tc, NULL, test_find_messagetype(f->msgs, "error65"));
    CuAssertIntEquals(tc, M_GWYRRD, unit_get_magic(u));

    test_teardown();
}

static void test_study_cost_magic(CuTest *tc) {
    unit * u;

    setup_study();
    u = test_create_unit(test_create_faction(), test_create_plain(0, 0));

    CuAssertIntEquals(tc, 100, study_cost(u, SK_MAGIC));
    set_level(u, SK_MAGIC, 1);
    CuAssertIntEquals(tc, 200, study_cost(u, SK_MAGIC));
    set_level(u, SK_MAGIC, 2);
    CuAssertIntEquals(tc, 350, study_cost(u, SK_MAGIC));
    set_level(u, SK_MAGIC, 29);
    CuAssertIntEquals(tc, 23300, study_cost(u, SK_MAGIC));
    set_level(u, SK_MAGIC, 27);
    CuAssertIntEquals(tc, 20350, study_cost(u, SK_MAGIC));

    config_set("skills.cost.magic", "100");
    CuAssertIntEquals(tc, 2*20350, study_cost(u, SK_MAGIC));

    test_teardown();
}

static void test_study_cost(CuTest *tc) {
    unit *u;
    const struct item_type *itype;

    setup_study();

    itype = test_create_silver();
    u = test_create_unit(test_create_faction(), test_create_plain(0, 0));
    scale_number(u, 2);
    u->thisorder = create_order(K_STUDY, u->faction->locale, skillnames[SK_ALCHEMY]);

    CuAssertIntEquals(tc, 200, study_cost(u, SK_ALCHEMY));
    config_set("skills.cost.alchemy", "50");
    CuAssertIntEquals(tc, 50, study_cost(u, SK_ALCHEMY));

    i_change(&u->items, itype, u->number * study_cost(u, SK_ALCHEMY));
    learn_inject();
    study_cmd(u, u->thisorder);
    learn_reset();
    CuAssertPtrEquals(tc, u, log_learners[0].u);
    CuAssertIntEquals(tc, SK_ALCHEMY, log_learners[0].sk);
    CuAssertIntEquals(tc, SKILL_DAYS_PER_WEEK * u->number, log_learners[0].days);
    CuAssertIntEquals(tc, 0, i_get(u->items, itype));
    test_teardown();
}

static void test_teach_magic(CuTest *tc) {
    unit *u, *ut;
    faction *f;
    const struct item_type *itype;

    setup_study();
    init_resources();
    itype = get_resourcetype(R_SILVER)->itype;
    f = test_create_faction();
    f->magiegebiet = M_GWYRRD;
    u = test_create_unit(f, test_create_plain(0, 0));
    u->thisorder = create_order(K_STUDY, f->locale, skillnames[SK_MAGIC]);
    i_change(&u->items, itype, study_cost(u, SK_MAGIC));
    ut = test_create_unit(f, u->region);
    set_level(ut, SK_MAGIC, TEACHDIFFERENCE);
    create_mage(ut, M_GWYRRD);
    ut->thisorder = create_order(K_TEACH, f->locale, itoa36(u->no));
    learn_inject();
    teach_cmd(ut, ut->thisorder);
    study_cmd(u, u->thisorder);
    learn_reset();
    CuAssertPtrEquals(tc, u, log_learners[0].u);
    CuAssertIntEquals(tc, SK_MAGIC, log_learners[0].sk);
    CuAssertIntEquals(tc, SKILL_DAYS_PER_WEEK * 2, log_learners[0].days);
    CuAssertIntEquals(tc, 0, i_get(u->items, itype));
    test_teardown();
}

static void test_teach_cmd(CuTest *tc) {
    unit *u, *ut;
    
    setup_study();
    init_resources();
    u = test_create_unit(test_create_faction(), test_create_plain(0, 0));
    scale_number(u, 10);
    u->thisorder = create_order(K_STUDY, u->faction->locale, "CROSSBOW");
    ut = test_create_unit(u->faction, u->region);
    set_level(ut, SK_CROSSBOW, TEACHDIFFERENCE);
    ut->thisorder = create_order(K_TEACH, u->faction->locale, itoa36(u->no));
    learn_inject();
    teach_cmd(ut, ut->thisorder);
    study_cmd(u, u->thisorder);
    learn_reset();
    CuAssertPtrEquals(tc, u, log_learners[0].u);
    CuAssertIntEquals(tc, SK_CROSSBOW, log_learners[0].sk);
    CuAssertIntEquals(tc, SKILL_DAYS_PER_WEEK * 2 * u->number, log_learners[0].days);
    test_teardown();
}

static void test_teach_not_found(CuTest *tc) {
    unit *u, *ut;
    
    test_setup();
    u = test_create_unit(test_create_faction(), test_create_plain(0, 0));
    ut = test_create_unit(u->faction, test_create_plain(1, 1));
    ut->thisorder = create_order(K_TEACH, u->faction->locale, itoa36(u->no));
    teach_cmd(ut, ut->thisorder);
    CuAssertPtrNotNull(tc, test_find_messagetype(ut->faction->msgs, "unitnotfound_id"));
    CuAssertPtrEquals(tc, NULL, test_find_messagetype(ut->faction->msgs, "teach_nolearn"));

    move_unit(u, ut->region, NULL);
    test_clear_messages(ut->faction);
    teach_cmd(ut, ut->thisorder);
    CuAssertPtrEquals(tc, NULL, test_find_messagetype(ut->faction->msgs, "unitnotfound_id"));
    CuAssertPtrNotNull(tc, test_find_messagetype(ut->faction->msgs, "teach_nolearn"));

    test_teardown();
}

static void test_teach_two(CuTest *tc) {
    unit *u1, *u2, *ut;
    
    setup_study();
    init_resources();
    u1 = test_create_unit(test_create_faction(), test_create_plain(0, 0));
    scale_number(u1, 5);
    u1->thisorder = create_order(K_STUDY, u1->faction->locale, "CROSSBOW");
    u2 = test_create_unit(u1->faction, u1->region);
    scale_number(u2, 5);
    u2->thisorder = create_order(K_STUDY, u2->faction->locale, "CROSSBOW");
    ut = test_create_unit(u1->faction, u1->region);
    set_level(ut, SK_CROSSBOW, TEACHDIFFERENCE);
    ut->thisorder = create_order(K_TEACH, ut->faction->locale, "%s %s", itoa36(u1->no), itoa36(u2->no));
    learn_inject();
    teach_cmd(ut, ut->thisorder);
    study_cmd(u1, u1->thisorder);
    study_cmd(u2, u2->thisorder);
    learn_reset();
    CuAssertPtrEquals(tc, u1, log_learners[0].u);
    CuAssertIntEquals(tc, SK_CROSSBOW, log_learners[0].sk);
    CuAssertIntEquals(tc, SKILL_DAYS_PER_WEEK * 2 * u1->number, log_learners[0].days);
    CuAssertPtrEquals(tc, u2, log_learners[1].u);
    CuAssertIntEquals(tc, SK_CROSSBOW, log_learners[1].sk);
    CuAssertIntEquals(tc, SKILL_DAYS_PER_WEEK * 2 * u2->number, log_learners[1].days);
    test_teardown();
}

static void test_teach_two_skills(CuTest *tc) {
    unit *u1, *u2, *ut;
    faction *f;
    region *r;

    setup_study();
    init_resources();
    f = test_create_faction();
    r = test_create_plain(0, 0);
    u1 = test_create_unit(f, r);
    scale_number(u1, 5);
    u1->thisorder = create_order(K_STUDY, f->locale, "CROSSBOW");
    u2 = test_create_unit(f, r);
    scale_number(u2, 5);
    u2->thisorder = create_order(K_STUDY, f->locale, "ENTERTAINMENT");
    ut = test_create_unit(f, r);
    set_level(ut, SK_ENTERTAINMENT, TEACHDIFFERENCE);
    set_level(ut, SK_CROSSBOW, TEACHDIFFERENCE);
    ut->thisorder = create_order(K_TEACH, f->locale, "%s %s", itoa36(u1->no), itoa36(u2->no));
    learn_inject();
    teach_cmd(ut, ut->thisorder);
    study_cmd(u1, u1->thisorder);
    study_cmd(u2, u2->thisorder);
    learn_reset();
    CuAssertPtrEquals(tc, u1, log_learners[0].u);
    CuAssertIntEquals(tc, SK_CROSSBOW, log_learners[0].sk);
    CuAssertIntEquals(tc, SKILL_DAYS_PER_WEEK * 2 * u1->number, log_learners[0].days);
    CuAssertPtrEquals(tc, u2, log_learners[1].u);
    CuAssertIntEquals(tc, SK_ENTERTAINMENT, log_learners[1].sk);
    CuAssertIntEquals(tc, SKILL_DAYS_PER_WEEK * 2 * u2->number, log_learners[1].days);
    test_teardown();
}

static void test_teach_one_to_many(CuTest *tc) {
    unit *u, *ut;

    setup_study();
    init_resources();
    u = test_create_unit(test_create_faction(), test_create_plain(0, 0));
    scale_number(u, 20);
    u->thisorder = create_order(K_STUDY, u->faction->locale, "CROSSBOW");
    ut = test_create_unit(u->faction, u->region);
    set_level(ut, SK_CROSSBOW, TEACHDIFFERENCE);
    ut->thisorder = create_order(K_TEACH, u->faction->locale, itoa36(u->no));
    learn_inject();
    teach_cmd(ut, ut->thisorder);
    study_cmd(u, u->thisorder);
    learn_reset();
    CuAssertPtrEquals(tc, u, log_learners[0].u);
    CuAssertIntEquals(tc, SK_CROSSBOW, log_learners[0].sk);
    CuAssertIntEquals(tc, SKILL_DAYS_PER_WEEK * 10 + SKILL_DAYS_PER_WEEK * u->number, log_learners[0].days);
    test_teardown();
}

static void test_teach_many_to_one(CuTest *tc) {
    unit *u, *u1, *u2;

    setup_study();
    init_resources();
    u = test_create_unit(test_create_faction(), test_create_plain(0, 0));
    scale_number(u, 20);
    u->thisorder = create_order(K_STUDY, u->faction->locale, "CROSSBOW");
    u1 = test_create_unit(u->faction, u->region);
    set_level(u1, SK_CROSSBOW, TEACHDIFFERENCE);
    u1->thisorder = create_order(K_TEACH, u->faction->locale, itoa36(u->no));
    u2 = test_create_unit(u->faction, u->region);
    set_level(u2, SK_CROSSBOW, TEACHDIFFERENCE);
    u2->thisorder = create_order(K_TEACH, u->faction->locale, itoa36(u->no));
    learn_inject();
    teach_cmd(u1, u1->thisorder);
    teach_cmd(u2, u2->thisorder);
    study_cmd(u, u->thisorder);
    learn_reset();
    CuAssertPtrEquals(tc, u, log_learners[0].u);
    CuAssertIntEquals(tc, SK_CROSSBOW, log_learners[0].sk);
    CuAssertIntEquals(tc, 2 * SKILL_DAYS_PER_WEEK * u->number, log_learners[0].days);
    test_teardown();
}

static void test_teach_message(CuTest *tc) {
    unit *u, *u1, *u2;
    attrib *a;
    teaching_info *teach;

    setup_study();
    init_resources();
    u = test_create_unit(test_create_faction(), test_create_plain(0, 0));
    scale_number(u, 20);
    u->thisorder = create_order(K_STUDY, u->faction->locale, "CROSSBOW");
    u1 = test_create_unit(test_create_faction(), u->region);
    set_level(u1, SK_CROSSBOW, TEACHDIFFERENCE);
    u1->thisorder = create_order(K_TEACH, u->faction->locale, itoa36(u->no));
    u2 = test_create_unit(test_create_faction(), u->region);
    ally_set(&u->faction->allies, u2->faction, HELP_GUARD);
    set_level(u2, SK_CROSSBOW, TEACHDIFFERENCE);
    u2->thisorder = create_order(K_TEACH, u->faction->locale, itoa36(u->no));
    CuAssertTrue(tc, !alliedunit(u, u1->faction, HELP_GUARD));
    CuAssertTrue(tc, alliedunit(u, u2->faction, HELP_GUARD));
    teach_cmd(u1, u1->thisorder);
    teach_cmd(u2, u2->thisorder);
    a = a_find(u->attribs, &at_learning);
    CuAssertPtrNotNull(tc, a);
    CuAssertPtrNotNull(tc, a->data.v);
    teach = (teaching_info *)a->data.v;
    CuAssertPtrNotNull(tc, teach->teachers);
    CuAssertIntEquals(tc, 600, teach->days);
    CuAssertIntEquals(tc, 2, selist_length(teach->teachers));
    CuAssertPtrEquals(tc, u1, selist_get(teach->teachers, 0));
    CuAssertPtrEquals(tc, u2, selist_get(teach->teachers, 1));
    study_cmd(u, u->thisorder);
    CuAssertPtrEquals(tc, NULL, test_find_messagetype(u1->faction->msgs, "teach_teacher"));
    CuAssertPtrNotNull(tc, test_find_messagetype(u2->faction->msgs, "teach_teacher"));
    CuAssertPtrNotNull(tc, test_find_messagetype(u->faction->msgs, "teach_student"));
    a = a_find(u->attribs, &at_learning);
    CuAssertPtrEquals(tc, NULL, a);
    test_teardown();
}

static void test_teach_many_to_many(CuTest *tc) {
    unit *s1, *s2, *t1, *t2;
    region *r;
    faction *f;

    setup_study();
    init_resources();
    f = test_create_faction();
    r = test_create_plain(0, 0);
    s1 = test_create_unit(f, r);
    scale_number(s1, 20);
    s1->thisorder = create_order(K_STUDY, f->locale, "CROSSBOW");
    s2 = test_create_unit(f, r);
    scale_number(s2, 10);
    s2->thisorder = create_order(K_STUDY, f->locale, "CROSSBOW");

    t1 = test_create_unit(f, r);
    set_level(t1, SK_CROSSBOW, TEACHDIFFERENCE);
    t1->thisorder = create_order(K_TEACH, f->locale, "%s %s", itoa36(s1->no), itoa36(s2->no));
    t2 = test_create_unit(f, r);
    scale_number(t2, 2);
    set_level(t2, SK_CROSSBOW, TEACHDIFFERENCE);
    t2->thisorder = create_order(K_TEACH, f->locale, "%s %s", itoa36(s1->no), itoa36(s2->no));
    learn_inject();
    teach_cmd(t1, t1->thisorder);
    teach_cmd(t2, t2->thisorder);
    study_cmd(s1, s1->thisorder);
    study_cmd(s2, s2->thisorder);
    learn_reset();
    CuAssertPtrEquals(tc, s1, log_learners[0].u);
    CuAssertIntEquals(tc, SK_CROSSBOW, log_learners[0].sk);
    CuAssertIntEquals(tc, 2 * SKILL_DAYS_PER_WEEK * s1->number, log_learners[0].days);
    CuAssertPtrEquals(tc, s2, log_learners[1].u);
    CuAssertIntEquals(tc, SK_CROSSBOW, log_learners[1].sk);
    CuAssertIntEquals(tc, 2 * SKILL_DAYS_PER_WEEK * s2->number, log_learners[1].days);
    test_teardown();
}

CuSuite *get_study_suite(void)
{
    CuSuite *suite = CuSuiteNew();
    SUITE_ADD_TEST(suite, test_study_cmd);
    SUITE_ADD_TEST(suite, test_study_cost);
    SUITE_ADD_TEST(suite, test_study_cost_magic);
    SUITE_ADD_TEST(suite, test_study_magic);
    SUITE_ADD_TEST(suite, test_teach_cmd);
    SUITE_ADD_TEST(suite, test_teach_not_found);
    SUITE_ADD_TEST(suite, test_teach_magic);
    SUITE_ADD_TEST(suite, test_teach_two);
    SUITE_ADD_TEST(suite, test_teach_one_to_many);
    SUITE_ADD_TEST(suite, test_teach_many_to_one);
    SUITE_ADD_TEST(suite, test_teach_many_to_many);
    SUITE_ADD_TEST(suite, test_teach_message);
    SUITE_ADD_TEST(suite, test_teach_two_skills);
    SUITE_ADD_TEST(suite, test_learn_skill_single);
    SUITE_ADD_TEST(suite, test_learn_skill_multi);
    SUITE_ADD_TEST(suite, test_study_no_teacher);
    SUITE_ADD_TEST(suite, test_study_with_teacher);
    SUITE_ADD_TEST(suite, test_study_with_bad_teacher);
    SUITE_ADD_TEST(suite, test_study_race_noteach);
    SUITE_ADD_TEST(suite, test_study_speed);
    SUITE_ADD_TEST(suite, test_academy_building);
    SUITE_ADD_TEST(suite, test_academy_bonus);
    SUITE_ADD_TEST(suite, test_demon_skillchange);
    SUITE_ADD_TEST(suite, test_demon_skillchange_hungry);
    SUITE_ADD_TEST(suite, test_study_bug_2194);
    SUITE_ADD_TEST(suite, test_check_student);
    return suite;
}
