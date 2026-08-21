/* Host unit test for the floating point maths behind the rotating triangle.
 *
 * The kernel enables SSE before any of this runs; on an ordinary machine it is
 * already on, so the same code can be checked here without an emulator. What
 * is checked is the arithmetic: the sine and cosine against known values, the
 * rotation against the quarter turns whose answers are exact, and the angle
 * against time.
 */
#include <math.h>
#include <stdio.h>

#include "geometry.h"

#define PI 3.14159265358979323846
#define TWO_PI (2.0 * PI)
#define TIMER_HZ 1193182u
#define SPEED 0.6

static int failures;

static void check(int condition, const char *what)
{
    if (condition) {
        printf("  ok    %s\n", what);
    } else {
        printf("  FAIL  %s\n", what);
        failures++;
    }
}

static int close_to(double value, double expected, double tolerance)
{
    const double difference = value - expected;
    return (difference < 0 ? -difference : difference) <= tolerance;
}

static void test_sin_cos(void)
{
    printf("sine and cosine agree with the values everyone knows\n");
    check(close_to(geo_sin(0.0), 0.0, 1e-12), "sin 0");
    check(close_to(geo_cos(0.0), 1.0, 1e-12), "cos 0");
    check(close_to(geo_sin(PI / 6.0), 0.5, 1e-9), "sin 30 degrees is a half");
    check(close_to(geo_cos(PI / 3.0), 0.5, 1e-9), "cos 60 degrees is a half");
    check(close_to(geo_sin(PI / 4.0), 0.7071067811865476, 1e-9), "sin 45 degrees");
    check(close_to(geo_sin(PI / 2.0), 1.0, 1e-9), "sin 90 degrees");
    check(close_to(geo_cos(PI / 2.0), 0.0, 1e-9), "cos 90 degrees");
    check(close_to(geo_sin(PI), 0.0, 1e-9), "sin 180 degrees");
    check(close_to(geo_cos(PI), -1.0, 1e-9), "cos 180 degrees");
    check(close_to(geo_sin(3.0 * PI / 2.0), -1.0, 1e-9), "sin 270 degrees");
    check(close_to(geo_cos(TWO_PI), 1.0, 1e-9), "cos a full turn");

    printf("and with the library across a whole turn\n");
    double worst = 0.0;
    for (int i = 0; i <= 3600; i++) {
        const double a = TWO_PI * i / 3600.0;
        double d = geo_sin(a) - sin(a);
        if (d < 0) d = -d;
        if (d > worst) worst = d;
        d = geo_cos(a) - cos(a);
        if (d < 0) d = -d;
        if (d > worst) worst = d;
    }
    printf("        worst error over 3601 angles: %.3g\n", worst);
    check(worst < 1e-9, "never off by as much as a billionth");

    printf("negative and very large angles still work\n");
    check(close_to(geo_sin(-PI / 2.0), -1.0, 1e-9), "minus 90 degrees");
    check(close_to(geo_sin(100.0 * TWO_PI + PI / 6.0), 0.5, 1e-7),
          "a hundred turns past 30 degrees");
    check(close_to(geo_cos(-100.0 * TWO_PI), 1.0, 1e-7), "a hundred turns backwards");

    printf("identities hold\n");
    for (int i = 0; i < 360; i += 7) {
        const double a = i * PI / 180.0;
        const double s = geo_sin(a), c = geo_cos(a);
        if (!close_to(s * s + c * c, 1.0, 1e-9)) {
            printf("  FAIL  sin squared plus cos squared is one, at %d degrees\n", i);
            failures++;
            return;
        }
    }
    check(1, "sin squared plus cos squared is one, every seven degrees");
}

static void test_wrap(void)
{
    printf("angles come back into one turn\n");
    check(close_to(geo_wrap_angle(0.0), 0.0, 1e-12), "zero");
    check(close_to(geo_wrap_angle(PI), PI, 1e-12), "half a turn is left alone");
    check(close_to(geo_wrap_angle(TWO_PI), 0.0, 1e-9), "a full turn becomes zero");
    check(close_to(geo_wrap_angle(TWO_PI + 1.0), 1.0, 1e-9), "a turn and a bit");
    check(close_to(geo_wrap_angle(-1.0), TWO_PI - 1.0, 1e-9), "a negative angle");
    check(geo_wrap_angle(1000.0 * TWO_PI + 2.0) >= 0.0, "a thousand turns is still positive");
    check(geo_wrap_angle(1000.0 * TWO_PI + 2.0) < TWO_PI, "and still inside one turn");
}

static void test_rotation(void)
{
    double x, y;

    printf("rotating a point about a centre\n");
    geo_rotate(100.0, 100.0, 150.0, 100.0, 0.0, &x, &y);
    check(close_to(x, 150.0, 1e-9) && close_to(y, 100.0, 1e-9), "no turn leaves it alone");

    geo_rotate(100.0, 100.0, 150.0, 100.0, PI / 2.0, &x, &y);
    check(close_to(x, 100.0, 1e-7) && close_to(y, 150.0, 1e-7), "a quarter turn");

    geo_rotate(100.0, 100.0, 150.0, 100.0, PI, &x, &y);
    check(close_to(x, 50.0, 1e-7) && close_to(y, 100.0, 1e-7), "a half turn");

    geo_rotate(100.0, 100.0, 150.0, 100.0, 3.0 * PI / 2.0, &x, &y);
    check(close_to(x, 100.0, 1e-7) && close_to(y, 50.0, 1e-7), "three quarters");

    geo_rotate(100.0, 100.0, 150.0, 100.0, TWO_PI, &x, &y);
    check(close_to(x, 150.0, 1e-7) && close_to(y, 100.0, 1e-7), "a full turn comes home");

    printf("the centre never moves and the radius never changes\n");
    geo_rotate(100.0, 100.0, 100.0, 100.0, 1.234, &x, &y);
    check(close_to(x, 100.0, 1e-9) && close_to(y, 100.0, 1e-9),
          "the centre rotated about itself stays put");

    double worst = 0.0;
    for (int i = 0; i < 720; i++) {
        const double a = TWO_PI * i / 720.0;
        geo_rotate(400.0, 300.0, 400.0, 230.0, a, &x, &y);   /* radius 70 */
        const double dx = x - 400.0, dy = y - 300.0;
        double r = sqrt(dx * dx + dy * dy) - 70.0;
        if (r < 0) r = -r;
        if (r > worst) worst = r;
    }
    printf("        worst radius drift over 720 angles: %.3g\n", worst);
    check(worst < 1e-9, "the radius holds all the way round");

    printf("nothing produces a value that is not a number\n");
    for (int i = -720; i <= 720; i += 13) {
        const double a = TWO_PI * i / 360.0;
        geo_rotate(0.0, 0.0, 1.0, 0.0, a, &x, &y);
        if (isnan(x) || isnan(y) || isinf(x) || isinf(y)) {
            printf("  FAIL  not a number at %d\n", i);
            failures++;
            return;
        }
    }
    check(1, "every angle from minus two turns to plus two turns is finite");
}

static void test_angle_from_time(void)
{
    printf("the angle follows the clock, not the loop\n");
    const double one_second = geo_angle_after(0.0, TIMER_HZ, TIMER_HZ, SPEED);
    check(close_to(one_second, SPEED, 1e-9), "one second is one second of turn");

    double stepped = 0.0;
    for (int i = 0; i < 1000; i++) {
        stepped = geo_angle_after(stepped, TIMER_HZ / 1000, TIMER_HZ, SPEED);
    }
    const double at_once = geo_angle_after(0.0, (TIMER_HZ / 1000) * 1000, TIMER_HZ, SPEED);
    printf("        thousand steps %.9f, one step %.9f\n", stepped, at_once);
    check(close_to(stepped, at_once, 1e-9), "a thousand small steps match one large one");

    check(close_to(geo_angle_after(1.0, 0, TIMER_HZ, SPEED), 1.0, 1e-12),
          "no time passing means no turn");
    check(close_to(geo_angle_after(1.0, TIMER_HZ, 0, SPEED), 1.0, 1e-12),
          "a timer with no frequency is refused");

    printf("it stays inside one turn however long it runs\n");
    double angle = 0.0;
    for (int i = 0; i < 20000; i++) {   /* about five and a half minutes */
        angle = geo_angle_after(angle, TIMER_HZ / 60, TIMER_HZ, SPEED);
        if (angle < 0.0 || angle >= TWO_PI || isnan(angle)) {
            printf("  FAIL  angle left its range at step %d: %f\n", i, angle);
            failures++;
            return;
        }
    }
    check(1, "twenty thousand steps stay in range");
}

static void test_triangle(void)
{
    struct triangle_screen shape, later;

    printf("the triangle sits where it was put\n");
    triangle_init(640, 700, 66);
    triangle_vertices(&shape);

    int sum_x = 0, sum_y = 0;
    for (int i = 0; i < TRIANGLE_VERTICES; i++) {
        sum_x += shape.x[i];
        sum_y += shape.y[i];
    }
    check(close_to(sum_x / 3.0, 640.0, 1.0), "its corners average to the centre across");
    check(close_to(sum_y / 3.0, 700.0, 1.0), "and to the centre down");

    for (int i = 0; i < TRIANGLE_VERTICES; i++) {
        const double dx = shape.x[i] - 640.0, dy = shape.y[i] - 700.0;
        if (!close_to(sqrt(dx * dx + dy * dy), 66.0, 1.0)) {
            printf("  FAIL  corner %d is not at the radius\n", i);
            failures++;
            return;
        }
    }
    check(1, "every corner is at the radius");

    printf("it turns\n");
    check(!triangle_advance(0, TIMER_HZ), "no time, no turn");
    check(triangle_advance(TIMER_HZ / 4, TIMER_HZ), "a quarter second moves it");
    triangle_vertices(&later);
    int moved = 0;
    for (int i = 0; i < TRIANGLE_VERTICES; i++) {
        if (later.x[i] != shape.x[i] || later.y[i] != shape.y[i]) {
            moved = 1;
        }
    }
    check(moved, "and the corners are somewhere new");

    printf("its centre stays put while it turns\n");
    double worst = 0.0;
    for (int step = 0; step < 400; step++) {
        triangle_advance(TIMER_HZ / 30, TIMER_HZ);
        triangle_vertices(&later);
        double cx = 0.0, cy = 0.0;
        for (int i = 0; i < TRIANGLE_VERTICES; i++) {
            cx += later.x[i] / 3.0;
            cy += later.y[i] / 3.0;
        }
        double drift = sqrt((cx - 640.0) * (cx - 640.0) + (cy - 700.0) * (cy - 700.0));
        if (drift > worst) worst = drift;
    }
    printf("        worst centre drift over 400 turns: %.3f pixels\n", worst);
    check(worst < 1.5, "the centre never wanders more than rounding explains");

    printf("it stays inside a screen it fits in\n");
    for (int step = 0; step < 400; step++) {
        triangle_advance(TIMER_HZ / 30, TIMER_HZ);
        triangle_vertices(&later);
        for (int i = 0; i < TRIANGLE_VERTICES; i++) {
            if (later.x[i] < 0 || later.x[i] >= 1280 || later.y[i] < 0 || later.y[i] >= 800) {
                printf("  FAIL  corner %d left the screen at %d, %d\n",
                       i, later.x[i], later.y[i]);
                failures++;
                return;
            }
        }
    }
    check(1, "four hundred turns and every corner is still on a 1280 by 800 screen");

    printf("rounding\n");
    check(geo_round(0.4) == 0 && geo_round(0.6) == 1, "up and down");
    check(geo_round(-0.4) == 0 && geo_round(-0.6) == -1, "and the same going negative");
    check(geo_round(2.5) == 3 && geo_round(-2.5) == -3, "a half rounds away from zero");
}

int main(void)
{
    test_sin_cos();
    test_wrap();
    test_rotation();
    test_angle_from_time();
    test_triangle();

    if (failures > 0) {
        printf("\n%d geometry check(s) FAILED\n", failures);
        return 1;
    }
    printf("\ngeometry checks passed\n");
    return 0;
}
