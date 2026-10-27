/*
 * Stock-generation host translation unit (S085 C17.3).
 *
 * What:       one translation unit for the generated recipe objects and the
 *             device point function, so their static geometry tables stay
 *             visible to stockWaveGen_point().
 * Why:        the generated fragments are separate files; compiling them
 *             individually would make stock_gen_length/harmonics private to
 *             another object.
 * Inputs:     build/gen/stock_geo.c, stock_rec.c, stock_sine.c, stock_gen.c.
 * Outputs:    host_stockWaveGen_point() and the tables.
 * Accessors:  make -C tools/dsp_test stock_gen.
 * Affiliates: tools/gen_stock_recipes.py, test_stock_gen.c.
 */
#include "stock_geo.c"
#include "stock_rec.c"
#include "stock_sine.c"
#include "stock_gen.c"
