#include "geometry.h"

#include <stddef.h>

#define PI      3.14159265358979323846
#define TWO_PI  6.28318530717958647692
#define HALF_PI 1.57079632679489661923
#define QUARTER_PI 0.78539816339744830961

/* How fast the triangle turns, in radians per second. Slow enough to watch. */
#define TRIANGLE_SPEED 0.6

/* Taylor series for sine and cosine, used only on [0, pi/4] where they
 * converge quickly. The angle is reduced into a quadrant first, because a
 * wider interval would need many more terms for the same accuracy.
 *
 * Six terms each. Five was tempting and wrong: the first term left out is
 * about x^11/11!, which at pi/4 is near two parts in a billion, and the tests
 * measured exactly that. One more multiply buys three more decimal places, so
 * the error is now around 1e-12, which is a ten thousandth of a pixel at any
 * radius this kernel will ever draw. */
static double poly_sin(double x)
{
    const double x2 = x * x;
    return x * (1.0 - x2 * (1.0 / 6.0
             - x2 * (1.0 / 120.0
             - x2 * (1.0 / 5040.0
             - x2 * (1.0 / 362880.0
             - x2 * (1.0 / 39916800.0))))));
}

static double poly_cos(double x)
{
    const double x2 = x * x;
    return 1.0 - x2 * (0.5
             - x2 * (1.0 / 24.0
             - x2 * (1.0 / 720.0
             - x2 * (1.0 / 40320.0
             - x2 * (1.0 / 3628800.0
             - x2 * (1.0 / 479001600.0))))));
}

double geo_wrap_angle(double angle)
{
    /* Subtract whole turns in one step rather than looping, so an angle that
     * has been accumulating for an hour costs no more than a fresh one. */
    const double turns = angle / TWO_PI;
    const int64_t whole = (int64_t)turns;
    double wrapped = angle - (double)whole * TWO_PI;

    if (wrapped < 0.0) {
        wrapped += TWO_PI;
    }
    /* Rounding can leave it exactly at a full turn; that is zero. */
    if (wrapped >= TWO_PI) {
        wrapped = 0.0;
    }
    return wrapped;
}

/* Both at once, because they share the reduction and the triangle needs both. */
static void sin_cos(double angle, double *sine, double *cosine)
{
    const double wrapped = geo_wrap_angle(angle);
    const int quadrant = (int)(wrapped / HALF_PI) & 3;
    const double into = wrapped - (double)quadrant * HALF_PI;

    double s, c;
    if (into <= QUARTER_PI) {
        s = poly_sin(into);
        c = poly_cos(into);
    } else {
        /* Past pi/4 the roles swap: sin(x) is cos(pi/2 - x). */
        const double rest = HALF_PI - into;
        s = poly_cos(rest);
        c = poly_sin(rest);
    }

    switch (quadrant) {
    case 0:  *sine = s;  *cosine = c;  break;
    case 1:  *sine = c;  *cosine = -s; break;
    case 2:  *sine = -s; *cosine = -c; break;
    default: *sine = -c; *cosine = s;  break;
    }
}

double geo_sin(double angle)
{
    double s, c;
    sin_cos(angle, &s, &c);
    return s;
}

double geo_cos(double angle)
{
    double s, c;
    sin_cos(angle, &s, &c);
    return c;
}

double geo_angle_after(double angle, uint64_t elapsed, uint64_t hz,
                       double radians_per_second)
{
    if (hz == 0) {
        return angle;
    }
    const double seconds = (double)elapsed / (double)hz;
    return geo_wrap_angle(angle + seconds * radians_per_second);
}

void geo_rotate(double centre_x, double centre_y, double x, double y,
                double angle, double *out_x, double *out_y)
{
    double sine, cosine;
    sin_cos(angle, &sine, &cosine);

    const double dx = x - centre_x;
    const double dy = y - centre_y;

    *out_x = centre_x + cosine * dx - sine * dy;
    *out_y = centre_y + sine * dx + cosine * dy;
}

int32_t geo_round(double value)
{
    return (int32_t)(value >= 0.0 ? value + 0.5 : value - 0.5);
}

/* --- the triangle --------------------------------------------------------- */

static double centre_x, centre_y, vertex_radius;
static double angle;
static struct triangle_screen drawn;
static bool placed;

/* Corners of an equilateral triangle, measured from straight up so it starts
 * pointing at the top of the screen. Screen y grows downwards, which is why
 * the first corner is negative. */
static const double corner_angle[TRIANGLE_VERTICES] = {
    -HALF_PI,
    -HALF_PI + TWO_PI / 3.0,
    -HALF_PI + 2.0 * TWO_PI / 3.0,
};

static void compute(struct triangle_screen *out)
{
    for (int i = 0; i < TRIANGLE_VERTICES; i++) {
        const double at = corner_angle[i];
        const double base_x = centre_x + vertex_radius * geo_cos(at);
        const double base_y = centre_y + vertex_radius * geo_sin(at);

        double x, y;
        geo_rotate(centre_x, centre_y, base_x, base_y, angle, &x, &y);
        out->x[i] = geo_round(x);
        out->y[i] = geo_round(y);
    }
}

void triangle_init(int32_t x, int32_t y, int32_t radius)
{
    centre_x = (double)x;
    centre_y = (double)y;
    vertex_radius = (double)radius;
    angle = 0.0;
    placed = true;
    compute(&drawn);
}

bool triangle_advance(uint64_t elapsed, uint64_t hz)
{
    if (!placed || elapsed == 0) {
        return false;
    }

    angle = geo_angle_after(angle, elapsed, hz, TRIANGLE_SPEED);

    struct triangle_screen next;
    compute(&next);

    bool moved = false;
    for (int i = 0; i < TRIANGLE_VERTICES; i++) {
        if (next.x[i] != drawn.x[i] || next.y[i] != drawn.y[i]) {
            moved = true;
        }
    }
    if (!moved) {
        return false;   /* too little turn to change a pixel */
    }

    drawn = next;
    return true;
}

void triangle_vertices(struct triangle_screen *out)
{
    if (out != NULL) {
        *out = drawn;
    }
}
