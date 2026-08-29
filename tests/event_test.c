/* Host tests for the M15 bounded event queue. */
#include <stdio.h>

#include "event.h"

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

int main(void)
{
    printf("events remain ordered across a circular queue\n");
    struct window_event_queue queue;
    window_event_queue_init(&queue);
    check(window_event_count(&queue) == 0, "a new queue is empty");
    struct window_event event = {
        .type = WINDOW_EVENT_KEY_DOWN,
        .data.key = { .ch = 'A', .code = WINDOW_KEY_NONE },
    };
    check(window_event_push(&queue, &event), "push the first event");
    event.data.key.ch = 'B';
    check(window_event_push(&queue, &event), "push the second event");
    struct window_event out;
    check(window_event_pop(&queue, &out) && out.data.key.ch == 'A',
          "the first event leaves first");
    check(window_event_pop(&queue, &out) && out.data.key.ch == 'B',
          "the second event follows");
    check(!window_event_pop(&queue, &out), "an empty queue refuses a pop");

    printf("overflow drops the newest event explicitly and safely\n");
    /* read is now two slots into the array, so filling the queue exercises
     * the physical end and wraps back to the beginning. */
    for (size_t i = 0; i < WINDOW_EVENT_CAPACITY; i++) {
        event.data.key.ch = (char)i;
        check(window_event_push(&queue, &event), "fill one bounded slot");
    }
    event.data.key.ch = 'Z';
    check(!window_event_push(&queue, &event), "one event too many is refused");
    check(window_event_count(&queue) == WINDOW_EVENT_CAPACITY,
          "overflow does not change queued events");
    check(window_event_dropped(&queue) == 1, "the dropped count is explicit");
    for (size_t i = 0; i < WINDOW_EVENT_CAPACITY; i++) {
        check(window_event_pop(&queue, &out) && out.data.key.ch == (char)i,
              "overflow preserved queue order");
    }

    printf("invalid queue operations are harmless\n");
    window_event_queue_init(NULL);
    event.type = WINDOW_EVENT_NONE;
    check(!window_event_push(&queue, &event), "an empty event type is refused");
    check(!window_event_push(NULL, &event), "no queue is refused");
    check(!window_event_push(&queue, NULL), "no event is refused");
    check(!window_event_pop(NULL, &out) && !window_event_pop(&queue, NULL),
          "invalid pops are refused");
    check(window_event_count(NULL) == 0 && window_event_dropped(NULL) == 0,
          "null queue counters are zero");

    if (failures > 0) {
        printf("\n%d event queue check(s) FAILED\n", failures);
        return 1;
    }
    printf("\nevent queue checks passed\n");
    return 0;
}
