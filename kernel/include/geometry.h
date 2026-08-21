/* One rotating triangle, and the small amount of arithmetic it needs.
 *
 * This is the only part of the kernel compiled with SSE enabled, so it is the
 * only part that may use floating point. Everything it exposes takes and
 * returns integers: a caller compiled without SSE cannot pass a double,
 * because the calling convention would put it in a register that caller is not
 * allowed to touch. Keeping the boundary integer only is what makes that
 * impossible to get wrong by accident.
 *
 * There is no matrix type, no vector type and no general maths library here.
 * A triangle rotating about its own centre needs a sine, a cosine, and a way
 * to turn elapsed time into an angle.
 */
#ifndef ME_GEOMETRY_H
#define ME_GEOMETRY_H

#include <stdbool.h>
#include <stdint.h>

#define TRIANGLE_VERTICES 3

/* Screen coordinates, already rounded. */
struct triangle_screen {
    int32_t x[TRIANGLE_VERTICES];
    int32_t y[TRIANGLE_VERTICES];
};

/* Places the triangle. The radius is the distance from the centre to each
 * vertex, so the triangle fits inside a circle of that radius wherever it is
 * turned to. */
void triangle_init(int32_t centre_x, int32_t centre_y, int32_t radius);

/* Turns the triangle by however far `elapsed` counts of an `hz` timer are
 * worth. Returns true when the rounded vertices have actually changed, so a
 * caller only redraws when the picture would differ. */
bool triangle_advance(uint64_t elapsed, uint64_t hz);

/* Where the corners are now. */
void triangle_vertices(struct triangle_screen *out);

/* --- the arithmetic underneath, exposed for testing ------------------------
 *
 * These take doubles, so only code compiled with SSE may call them. That is
 * this file's own source, and the host tests.
 */

double geo_sin(double angle);
double geo_cos(double angle);

/* Brings any angle into [0, 2*pi) without a loop, so a huge angle costs the
 * same as a small one. */
double geo_wrap_angle(double angle);

/* The angle after `elapsed` counts of an `hz` timer at a given speed. */
double geo_angle_after(double angle, uint64_t elapsed, uint64_t hz,
                       double radians_per_second);

/* Rotates one point about a centre. The standard rotation, written out. */
void geo_rotate(double centre_x, double centre_y, double x, double y,
                double angle, double *out_x, double *out_y);

/* Rounds to the nearest whole number, away from zero at the half. */
int32_t geo_round(double value);

#endif /* ME_GEOMETRY_H */
