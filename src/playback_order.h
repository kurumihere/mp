#ifndef MP_PLAYBACK_ORDER_H
#define MP_PLAYBACK_ORDER_H

#include <stdbool.h>
#include <stddef.h>

typedef size_t (*Playback_Random_Index)(size_t upper_bound, void *context);

typedef struct {
    size_t *indices;
    size_t count;
    size_t position;
    bool shuffled;
} Playback_Order;

void playback_order_init(Playback_Order *order);
void playback_order_uninit(Playback_Order *order);

bool playback_order_reset(Playback_Order *order, size_t count, size_t current,
                          bool shuffled, Playback_Random_Index random_index,
                          void *random_context);
bool playback_order_set_shuffled(Playback_Order *order, size_t current,
                                 bool shuffled,
                                 Playback_Random_Index random_index,
                                 void *random_context);
bool playback_order_select(Playback_Order *order, size_t index,
                           Playback_Random_Index random_index,
                           void *random_context);

bool playback_order_next(Playback_Order *order, bool repeat_all,
                         Playback_Random_Index random_index,
                         void *random_context, size_t *index);
bool playback_order_previous(Playback_Order *order, bool repeat_all,
                             size_t *index);

bool playback_order_can_next(const Playback_Order *order, bool repeat_all);
bool playback_order_can_previous(const Playback_Order *order, bool repeat_all);

#endif
