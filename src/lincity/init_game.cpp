/* ----------------------------------------------------------------------
 * init_game.cpp
 * This file is part of lincity-ng
 * see COPYING for license, and CREDITS for authors
 * ----------------------------------------------------------------------
 */

// This was part of simulate.cpp. 
// Moved in new file for clarification
//
// (re)initialise engine and UI data when
//  - load a saved game (or a scenario)
//  - start a random village (or a  void map)
//

#include "init_game.h"
#include "fileutil.h"
#include "simulate.h"
#include "gui_interface/shared_globals.h"
#include "lctypes.h"
#include "lin-city.h"
#include "engglobs.h"
#include "gui_interface/screen_interface.h"
#include "power.h"
#include "stats.h"
#include "gui_interface/pbar_interface.h"
#include "modules/all_modules.h"
#include "transport.h"

/* Private functions prototypes */

static void init_mappoint_array(void);
static void initialize_tax_rates(void);
static void nullify_mappoint(int x, int y);
static void random_start(int *originx, int *originy);
static void coal_reserve_setup(void);
static void ore_reserve_setup(void);
static void setup_river(void);
static void setup_river2(int x, int y, int d, int alt, int mountain);
static void setup_ground(void);

#define IS_RIVER(x,y) (MP_INFO(x,y).flags & FLAG_IS_RIVER)

/* ---------------------------------------------------------------------- *
 * Public Functions
 * ---------------------------------------------------------------------- */
void clear_game(void)
{
    int x, y, i, p;

    init_mappoint_array ();
    initialize_tax_rates ();
    init_inventory();

    // Clear engine and UI data.
    for (y = 0; y < WORLD_SIDE_LEN; y++) {
        for (x = 0; x < WORLD_SIDE_LEN; x++) {
            nullify_mappoint(x, y);
        }
    }
    total_time = 0;
    coal_survey_done = 0;
    numof_shanties = 0;
    numof_communes = 0;
    numof_substations = 0;
    numof_health_centres = 0;
    numof_markets = 0;
    max_pop_ever = 0;
    total_evacuated = 0;
    total_births = 0;
    total_money = 0;
    tech_level = 0;
    highest_tech_level = 0;
    rockets_launched = 0;
    rockets_launched_success = 0;
    init_inventory();
    update_avail_modules(0);

    use_waterwell = true; // NG 1.91 : AL1
                          // unused now (it was used in branch waterwell)
                          // but useful to know how to add an optional module, so keep it for a while.
    numof_waterwell = 0;
    global_aridity = 0;
    global_mountainity =0;

    highest_tech_level = 0;
    total_pollution_deaths = 0;
    pollution_deaths_history = 0;
    total_starve_deaths = 0;
    starve_deaths_history = 0;
    total_unemployed_years = 0;
    unemployed_history = 0;
    /* Al1. NG 1.1 is this enough ? Are all global variables reseted ? */

    // UI stuff
    /* TODO check reset screen, sustain info ... */
    given_scene[0] = 0;
    for( i = 0; i < monthgraph_size; i++ ){
        monthgraph_pop[i] = 0;
        monthgraph_starve[i] = 0;
        monthgraph_nojobs[i] = 0;
        monthgraph_ppool[i] = 0;
    } 
    // reset PBARS
    // FIXME AL1 NG 1.92.svn pbars are reseted only 
    //          when we build something
    //          when some building already exist and modify one value
    //
    /* AL1 i don't understand why this does not work
    init_pbars(); // AL1: Why is this not enough and why do we need additional stuff ?
    */
 
    /*
   */
    housed_population=0;
    tech_level=0;
    tfood_in_markets=0;
    tjobs_in_markets=0;
    tcoal_in_markets=0;
    tgoods_in_markets=0;
    tore_in_markets=0;
    tsteel_in_markets=0;
    total_money=0;

    init_pbars();
    for (p = 0; p < NUM_PBARS; p++)
        pbars[p].data_size = PBAR_DATA_SIZE;

    for (p = 0; p < NUM_PBARS; p++) {
        pbars[p].oldtot = 0;
        pbars[p].diff = 0;
    }
        
    for (x = 0; x < PBAR_DATA_SIZE; x++) {
        update_pbar (PPOP, housed_population, 1);
        update_pbar (PTECH, tech_level, 1);
        update_pbar (PFOOD, tfood_in_markets , 1);
        update_pbar (PJOBS, tjobs_in_markets , 1);
        update_pbar (PCOAL, tcoal_in_markets , 1);
        update_pbar (PGOODS, tgoods_in_markets  , 1);
        update_pbar (PORE, tore_in_markets  , 1);
        update_pbar (PSTEEL, tsteel_in_markets , 1);
        update_pbar (PMONEY, total_money, 1);
    }
    data_last_month = 1;
    update_pbars_monthly();
}

void new_city(int *originx, int *originy, int random_village)
{
    clear_game();
    coal_reserve_setup();

    global_mountainity= 10 + rand () % 300; // roughly water slope = 30m / 1km (=from N to S)
    setup_river();
    setup_ground();
    setup_land();
    ore_reserve_setup();
    init_pbars();

    /* Initial population is 100 for empty board or 200 
       for random village (100 are housed). */
    people_pool = 100;

    if (random_village != 0) {
        random_start(originx, originy);
        update_pbar(PPOP, 200, 1);      /* So pbars don't flash */
    } else {
        *originx = *originy = WORLD_SIDE_LEN / 2;
        update_pbar(PPOP, 100, 1);
    }
    connect_transport(1, 1, WORLD_SIDE_LEN - 2, WORLD_SIDE_LEN - 2);
    /* Fix desert frontier for old saved games and scenarios */
    desert_frontier(0, 0, WORLD_SIDE_LEN, WORLD_SIDE_LEN);

    refresh_pbars(); // AL1: does nothing in NG !
}

void setup_land(void)
{
    int x, y, xw, yw;
    int aridity = rand() % 450 - 150;

    global_aridity = aridity;
    

    for (y = 0; y < WORLD_SIDE_LEN; y++) {
        for (x = 0; x < WORLD_SIDE_LEN; x++) {
            int d2w_min = 2 * WORLD_SIDE_LEN * WORLD_SIDE_LEN;
            int r;
            int arid = aridity;

            /* test against IS_RIVER to prevent terrible recursion */
            if (IS_RIVER(x, y) || !GROUP_IS_BARE(MP_GROUP(x, y)))
                continue;

            for (yw = 0; yw < WORLD_SIDE_LEN; yw++) {
                for (xw = 0; xw < WORLD_SIDE_LEN; xw++) {
                    int d2w;
                    if (!IS_RIVER(xw, yw))
                        continue;
                    d2w = (xw - x) * (xw - x) + (yw - y) * (yw - y);
                    if (d2w < d2w_min)
                        d2w_min = d2w;
                    /* TODO ? Store square of distance to river for each tile */
                }
            }

            /* near river lower aridity */
            if (aridity > 0) {
                if (d2w_min < 5)
                    arid = aridity / 3;
                else if (d2w_min < 17)
                    arid = (aridity * 2) / 3;
            }
            r = rand() % (d2w_min / 3 + 1) + arid;
            ground[x][y].ecotable=r;
            /* needed to setup quasi randome land. The flag is set below */
            MP_INFO(x, y).flags |= FLAG_HAS_UNDERGROUND_WATER;
            do_rand_ecology(x,y);
            MP_POL(x, y) = 0;

            /* preserve rivers, so that we can connect port later */
            if (MP_TYPE(x, y) == CST_WATER) {
                int navigable = MP_INFO(x, y).flags & FLAG_IS_RIVER;
                set_mappoint(x, y, CST_WATER);
                MP_INFO(x, y).flags |= navigable;
                MP_INFO(x, y).flags |= FLAG_HAS_UNDERGROUND_WATER;
            }
            /* set undergroung water according to first random land setup */
            if (MP_TYPE(x, y) == CST_DESERT) {
                MP_INFO(x, y).flags &= (0xffffffff - FLAG_HAS_UNDERGROUND_WATER);
            }
        }
    }
    for (y = 0; y < WORLD_SIDE_LEN; y++)
        for (x = 0; x < WORLD_SIDE_LEN; x++)
            if (MP_TYPE(x, y) == CST_WATER)
                MP_INFO(x, y).flags |= FLAG_HAS_UNDERGROUND_WATER;

    connect_rivers();
}

/* ---------------------------------------------------------------------- *
 * Private Functions
 * ---------------------------------------------------------------------- */
static void initialize_tax_rates(void)
{
    income_tax_rate = INCOME_TAX_RATE;
    coal_tax_rate = COAL_TAX_RATE;
    goods_tax_rate = GOODS_TAX_RATE;
    dole_rate = DOLE_RATE;
    transport_cost_rate = TRANSPORT_COST_RATE;
    import_cost_rate = IM_PORT_COST_RATE;
}

static void init_mappoint_array(void)
{
    int x;
    for (x = 0; x < WORLD_SIDE_LEN; x++) {
        mappoint_array_x[x] = x;
        mappoint_array_y[x] = x;
    }
}

static void coal_reserve_setup(void)
{
    int i, j, x, y, xx, yy;
    for (i = 0; i < NUMOF_COAL_RESERVES / 5; i++) {
        x = (rand() % (WORLD_SIDE_LEN - 12)) + 6;
        y = (rand() % (WORLD_SIDE_LEN - 10)) + 6;
        do {
            xx = (rand() % 3) - 1;
            yy = (rand() % 3) - 1;
        }
        while (xx == 0 && yy == 0);
        for (j = 0; j < 5; j++) {
            MP_INFO(x, y).coal_reserve += rand() % COAL_RESERVE_SIZE;
            x += xx;
            y += yy;
        }
    }
}

static void ore_reserve_setup(void)
{
    int x, y;
    for (y = 0; y < WORLD_SIDE_LEN; y++)
        for (x = 0; x < WORLD_SIDE_LEN; x++)
            MP_INFO(x, y).ore_reserve = ORE_RESERVE;
}

static void setup_river(void)
{
    int x, y, i, j;
    int alt = 1; //lowest altitude in the map = surface of the river at mouth.
    x = (1 * WORLD_SIDE_LEN + rand() % WORLD_SIDE_LEN) / 3;
    y = WORLD_SIDE_LEN - 1;
    ground[x][y].water_alt = alt; // 1 unit = 1 cm , 
                        //for rivers .water_alt = .altitude = surface of the water
                        //for "earth tile" .water_alt = alt of underground water
                        //                 .altitude = alt of the ground
                        //            so .water_alt <= .altitude

    /* Mouth of the river, 3 tiles wide, 6 + %12 long */
    i = (rand() % 12) + 6;
    for (j = 0; j < i; j++) {
        x += (rand() % 3) - 1;
        MP_TYPE(x, y) = CST_WATER;
        MP_GROUP(x, y) = GROUP_WATER;
        MP_INFO(x, y).flags |= FLAG_IS_RIVER;
        ground[x][y].altitude=alt;

        MP_TYPE(x + 1, y) = CST_WATER;
        MP_GROUP(x + 1, y) = GROUP_WATER;
        MP_INFO(x + 1, y).flags |= FLAG_IS_RIVER;
        ground[x + 1][y].altitude=alt;

        MP_TYPE(x - 1, y) = CST_WATER;
        MP_GROUP(x - 1, y) = GROUP_WATER;
        MP_INFO(x - 1, y).flags |= FLAG_IS_RIVER;
        ground[x -1][y].altitude=alt;

        y--;
        alt += 1; // wide river, so very small slope
    }

    MP_TYPE(x, y) = CST_WATER;
    MP_GROUP(x, y) = GROUP_WATER;
    MP_INFO(x, y).flags |= FLAG_IS_RIVER;
    ground[x][y].altitude=alt;

    MP_TYPE(x + 1, y) = CST_WATER;
    MP_GROUP(x + 1, y) = GROUP_WATER;
    MP_INFO(x + 1, y).flags |= FLAG_IS_RIVER;
    ground[x + 1][y].altitude=alt;

    MP_TYPE(x - 1, y) = CST_WATER;
    MP_GROUP(x - 1, y) = GROUP_WATER;
    MP_INFO(x - 1, y).flags |= FLAG_IS_RIVER;
    ground[x -1][y].altitude=alt;

    alt += 2;

#ifdef DEBUG
    fprintf(stderr," x= %d, y=%d, altitude = %d, mountainity = %d\n", x, y, alt, global_mountainity);
#endif
    setup_river2(x - 1, y, -1, alt, global_mountainity); /* left tributary */
    setup_river2(x + 1, y, 1, alt, global_mountainity);  /* right tributary */
}

static void setup_river2(int x, int y, int d, int alt, int mountain)
{
    int i, j, r;
    i = (rand() % 55) + 15;
    for (j = 0; j < i; j++) {
        r = (rand() % 3) - 1 + (d * (rand() % 3));
        if (r < -1) {
            alt += rand() % (mountain / 10);
            r = -1;
        } else if (r > 1) {
            alt += rand() % (mountain / 10);
            r = 1;
        }
        x += r;
        if (!GROUP_IS_BARE(MP_GROUP(x + (d + d), y))
            || !GROUP_IS_BARE(MP_GROUP(x + (d + d + d), y)))
            return;
        if (x > 5 && x < WORLD_SIDE_LEN - 5) {
            MP_TYPE(x, y) = CST_WATER;
            MP_GROUP(x, y) = GROUP_WATER;
            MP_INFO(x, y).flags |= FLAG_IS_RIVER;
            ground[x][y].altitude = alt;
            alt += rand() % (mountain / 10);

            MP_TYPE(x + d, y) = CST_WATER;
            MP_GROUP(x + d, y) = GROUP_WATER;
            MP_INFO(x + d, y).flags |= FLAG_IS_RIVER;
            ground[x + d][y].altitude = alt;
            alt += rand () % (mountain / 10);
        }
        if (--y < 10 || x < 5 || x > WORLD_SIDE_LEN - 5)
            break;
    }
#ifdef DEBUG
    fprintf(stderr," x= %d, y=%d, altitude = %d\n", x, y, alt);
#endif

    if (y > 20) {
        if (x > 5 && x < WORLD_SIDE_LEN - 5) {
#ifdef DEBUG
            fprintf(stderr," x= %d, y=%d, altitude = %d\n", x, y, alt);
#endif
            setup_river2(x, y, -1, alt, (mountain * 3)/2 );
        }
        if (x > 5 && x < WORLD_SIDE_LEN - 5) {
#ifdef DEBUG
            fprintf(stderr," x= %d, y=%d, altitude = %d\n", x, y, alt);
#endif
            setup_river2(x, y, 1, alt, (mountain *3)/2 );
        }
    }
}

static void setup_ground(void)
{
    int x,y;
    int hmax =0;
    /* fill the corrects fields: ground[x][y).stuff, global_aridity, global_mountainity */
    /* currently only dummy things in order to compile */

    /* FIXME: AL1 i did it ugly: should not use ground struct for tmp */
#define TMP(x,y) ground[x][y].int4

    for (x = 1; x < WORLD_SIDE_LEN - 1; x++) {
         for (y = 1; y < WORLD_SIDE_LEN - 1; y++) {
                if ( !IS_RIVER(x,y) ) {
                    ALT(x,y) = 0;
                    TMP(x,y) = 0;
                } else {
                    ground[x][y].water_alt = ALT(x,y);
                    //shore is higher than water
                    ALT(x,y) += 10 + rand() % (global_mountainity/7);
                    TMP(x,y) = ALT(x,y);
                    if (ALT(x,y) >= hmax)
                        hmax = ALT(x,y);
                }
         }
    }
#ifdef DEBUG
    fprintf(stderr,"\n river max = %d\n\n", hmax);
    hmax=0;
#endif

    for (int i =0; i < 90; i++ ) {
        int tot_cnt = 0;
        for (x = 1; x < WORLD_SIDE_LEN - 1; x++) {
            for (y = 1; y < WORLD_SIDE_LEN - 1; y++) {
                if ( ALT(x,y) != 0 )
                    continue;
                int count = 0;
                int lmax = 0;
                tot_cnt ++;
                for ( int k = -1; k <= 1; k++ )
                    for ( int l = -1; l <= 1; l++) 
                        if ( ALT(x+k, y+l) != 0 ) {
                            count ++;
                            if ( ALT(x+k, y+l) >= lmax )
                                lmax = ALT(x+k, y+l);
                        }

                if (count != 0)
                    TMP(x,y) = lmax + rand () % (global_mountainity/3);

                if (TMP(x,y) >= hmax)
                    hmax = TMP(x,y);
            }
        }
        for (x = 1; x < WORLD_SIDE_LEN - 1; x++)
            for (y = 1; y < WORLD_SIDE_LEN - 1; y++)
                ALT(x,y)=TMP(x,y);

#ifdef DEBUG
        if ( (i%5) == 1 )
        fprintf(stderr," i= %2d, alt max = %d, tot_cnt = %d\n", i, hmax, tot_cnt);
#endif
    }
}


static void nullify_mappoint(int x, int y)
{
    MP_TYPE(x, y) = CST_GREEN;
    MP_GROUP(x, y) = GROUP_BARE;
    MP_SIZE(x, y) = 1;
    MP_POL(x, y) = 0;
    MP_INFO(x, y).population = 0;
    MP_INFO(x, y).flags = 0;
    MP_INFO(x, y).coal_reserve = 0;
    MP_INFO(x, y).ore_reserve = 0;
    MP_INFO(x, y).int_1 = 0;
    MP_INFO(x, y).int_2 = 0;
    MP_INFO(x, y).int_3 = 0;
    MP_INFO(x, y).int_4 = 0;
    MP_INFO(x, y).int_5 = 0;
    MP_INFO(x, y).int_6 = 0;
    MP_INFO(x, y).int_7 = 0;

    ground[x][y].altitude = 0;
    ground[x][y].ecotable = 0;
    ground[x][y].wastes = 0;
    ground[x][y].pollution = 0;
    ground[x][y].water_alt = 0;
    ground[x][y].water_pol = 0;
    ground[x][y].water_wast = 0;
    ground[x][y].water_next = 0;
    ground[x][y].int1 = 0;
    ground[x][y].int2 = 0;
    ground[x][y].int3 = 0;
    ground[x][y].int4 = 0;

}

static void random_start(int *originx, int *originy)
{
    int x, y, xx, yy, flag, watchdog;

    /* first find a place that has some water. */
    watchdog = 90;              /* if too many tries, random placement. */
    do {
        do {
            xx = rand() % (WORLD_SIDE_LEN - 25);
            yy = rand() % (WORLD_SIDE_LEN - 25);
            flag = 0;
            for (y = yy + 2; y < yy + 23; y++)
                for (x = xx + 2; x < xx + 23; x++)
                    if (IS_RIVER(x, y)) {
                        flag = 1;
                        x = xx + 23;    /* break out of loop */
                        y = yy + 23;    /* break out of loop */
                    }
        } while (flag == 0 && (--watchdog) > 1);
        for (y = yy + 4; y < yy + 22; y++)
            for (x = xx + 4; x < xx + 22; x++)
                /* Don't put the village on a river, but don't care of
                 * isolated random water tiles putted by setup_land
                 */
                if (IS_RIVER(x, y)) {
                    flag = 0;
                    x = xx + 22;        /* break out of loop */
                    y = yy + 22;        /* break out of loop */
                }
    } while (flag == 0 && (--watchdog) > 1);
#ifdef DEBUG
    fprintf(stderr, "random village watchdog = %i\n", watchdog);
#endif

    /* These are going to be the main_screen_origin? vars */
    *originx = xx;
    *originy = yy;

    /*  Draw the start scene. */
    set_mappoint(xx + 5, yy + 5, CST_FARM_O0);
    /* The first two farms have more underground water */
    for (int i = 0; i < MP_SIZE(xx + 5, yy + 5); i++)
        for (int j = 0; j < MP_SIZE(xx + 5, yy + 5); j++)
            if (!HAS_UGWATER(xx + 5 + i, yy + 5 + j) && (rand() % 2))
                MP_INFO(xx + 5 + i, yy + 5 + j).flags |= FLAG_HAS_UNDERGROUND_WATER;

    set_mappoint(xx + 9, yy + 6, CST_RESIDENCE_ML);
    MP_INFO(xx + 9, yy + 6).population = 50;
    MP_INFO(xx + 9, yy + 6).flags |= (FLAG_FED + FLAG_EMPLOYED + FLAG_WATERWELL_COVER);

    set_mappoint(xx + 9, yy + 9, CST_POTTERY_0);

    set_mappoint(xx + 16, yy + 9, CST_WATERWELL);

    set_mappoint(xx + 14, yy + 6, CST_RESIDENCE_ML);
    MP_INFO(xx + 14, yy + 6).population = 50;
    MP_INFO(xx + 14, yy + 6).flags |= (FLAG_FED + FLAG_EMPLOYED + FLAG_WATERWELL_COVER);

    /* The first two farms have more underground water */
    set_mappoint(xx + 17, yy + 5, CST_FARM_O0);
    for (int i = 0; i < MP_SIZE(xx + 17, yy + 5); i++)
        for (int j = 0; j < MP_SIZE(xx + 17, yy + 5); j++)
            if (!HAS_UGWATER(xx + 17 + i, yy + 5 + j) && (rand() % 2))
                MP_INFO(xx + 17 + i, yy + 5 + j).flags |= FLAG_HAS_UNDERGROUND_WATER;

    set_mappoint(xx + 14, yy + 9, CST_MARKET_EMPTY);
    marketx[numof_markets] = xx + 14;
    markety[numof_markets] = yy + 9;
    numof_markets++;
    /* Bootstrap markets with some stuff. */
    MP_INFO(xx + 14, yy + 9).int_1 = 2000;
    MP_INFO(xx + 14, yy + 9).int_2 = 10000;
    MP_INFO(xx + 14, yy + 9).int_3 = 100;
    MP_INFO(xx + 14, yy + 9).int_5 = 10000;
    MP_INFO(xx + 14, yy + 9).flags
        |= (FLAG_MB_FOOD + FLAG_MS_FOOD + FLAG_MB_JOBS
            + FLAG_MS_JOBS + FLAG_MB_COAL + FLAG_MS_COAL + FLAG_MB_ORE
            + FLAG_MS_ORE + FLAG_MB_GOODS + FLAG_MS_GOODS + FLAG_MB_STEEL + FLAG_MS_STEEL);

    /* build tracks */
    for (x = 2; x < 23; x++) {
        set_mappoint(xx + x, yy + 11, CST_TRACK_LR);
        MP_INFO(xx + x, yy + 11).flags |= FLAG_IS_TRANSPORT;
    }
    for (y = 2; y < 11; y++) {
        set_mappoint(xx + 13, yy + y, CST_TRACK_LR);
        MP_INFO(xx + 13, yy + y).flags |= FLAG_IS_TRANSPORT;
    }
    for (y = 12; y < 23; y++) {
        set_mappoint(xx + 15, yy + y, CST_TRACK_LR);
        MP_INFO(xx + 15, yy + y).flags |= FLAG_IS_TRANSPORT;
    }

    /* build communes */
    set_mappoint(xx + 6, yy + 12, CST_COMMUNE_1);
    set_mappoint(xx + 6, yy + 17, CST_COMMUNE_1);
    set_mappoint(xx + 11, yy + 12, CST_COMMUNE_1);
    set_mappoint(xx + 11, yy + 17, CST_COMMUNE_1);
    set_mappoint(xx + 16, yy + 12, CST_COMMUNE_1);
    set_mappoint(xx + 16, yy + 17, CST_COMMUNE_1);
}


