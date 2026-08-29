/* Bounded high-level window events. Device packets are not app APIs. */
#ifndef ME_EVENT_H
#define ME_EVENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define WINDOW_EVENT_CAPACITY 32
#define WINDOW_MOUSE_LEFT   0x01u
#define WINDOW_MOUSE_RIGHT  0x02u
#define WINDOW_MOUSE_MIDDLE 0x04u

enum window_event_type {
    WINDOW_EVENT_NONE = 0,
    WINDOW_EVENT_MOUSE_MOVE,
    WINDOW_EVENT_MOUSE_DOWN,
    WINDOW_EVENT_MOUSE_UP,
    WINDOW_EVENT_KEY_DOWN,
    WINDOW_EVENT_FOCUS_GAINED,
    WINDOW_EVENT_FOCUS_LOST,
};

enum window_key_code {
    WINDOW_KEY_NONE = 0,
    WINDOW_KEY_ENTER,
    WINDOW_KEY_ESCAPE,
    WINDOW_KEY_BACKSPACE,
    WINDOW_KEY_TAB,
    WINDOW_KEY_UP,
    WINDOW_KEY_DOWN,
    WINDOW_KEY_LEFT,
    WINDOW_KEY_RIGHT,
};

struct window_event {
    enum window_event_type type;
    union {
        struct {
            int32_t x;
            int32_t y;
            uint8_t buttons;
        } mouse;
        struct {
            char ch;
            enum window_key_code code;
        } key;
    } data;
};

struct window_event_queue {
    struct window_event events[WINDOW_EVENT_CAPACITY];
    size_t read;
    size_t count;
    uint32_t dropped;
};

void window_event_queue_init(struct window_event_queue *queue);
bool window_event_push(struct window_event_queue *queue,
                       const struct window_event *event);
bool window_event_pop(struct window_event_queue *queue,
                      struct window_event *event);
size_t window_event_count(const struct window_event_queue *queue);
uint32_t window_event_dropped(const struct window_event_queue *queue);

#endif /* ME_EVENT_H */
