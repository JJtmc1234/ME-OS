#include "event.h"

void window_event_queue_init(struct window_event_queue *queue)
{
    if (queue != NULL) {
        *queue = (struct window_event_queue){0};
    }
}

bool window_event_push(struct window_event_queue *queue,
                       const struct window_event *event)
{
    if (queue == NULL || event == NULL || event->type == WINDOW_EVENT_NONE) {
        return false;
    }
    if (queue->count >= WINDOW_EVENT_CAPACITY) {
        queue->dropped++;
        return false;
    }
    const size_t write = (queue->read + queue->count) % WINDOW_EVENT_CAPACITY;
    queue->events[write] = *event;
    queue->count++;
    return true;
}

bool window_event_pop(struct window_event_queue *queue,
                      struct window_event *event)
{
    if (queue == NULL || event == NULL || queue->count == 0) {
        return false;
    }
    *event = queue->events[queue->read];
    queue->read = (queue->read + 1) % WINDOW_EVENT_CAPACITY;
    queue->count--;
    return true;
}

size_t window_event_count(const struct window_event_queue *queue)
{
    return queue == NULL ? 0 : queue->count;
}

uint32_t window_event_dropped(const struct window_event_queue *queue)
{
    return queue == NULL ? 0 : queue->dropped;
}
