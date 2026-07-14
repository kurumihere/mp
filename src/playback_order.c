#include "playback_order.h"

#include <stdint.h>
#include <stdlib.h>

static size_t random_offset(size_t upper_bound,
                            Playback_Random_Index random_index,
                            void *random_context)
{
    if (upper_bound == 0 || random_index == NULL) return 0;

    return random_index(upper_bound, random_context) % upper_bound;
}

static void shuffle_range(size_t *indices, size_t first, size_t count,
                          Playback_Random_Index random_index,
                          void *random_context)
{
    for (size_t i = count; i > first + 1; --i) {
        size_t other =
            first + random_offset(i - first, random_index, random_context);
        size_t temporary = indices[i - 1];
        indices[i - 1] = indices[other];
        indices[other] = temporary;
    }
}

static void arrange_from_current(Playback_Order *order, size_t current,
                                 Playback_Random_Index random_index,
                                 void *random_context)
{
    for (size_t i = 0; i < order->count; ++i) {
        order->indices[i] = i;
    }

    if (!order->shuffled) {
        order->position = current;
        return;
    }

    order->indices[current] = 0;
    order->indices[0] = current;
    shuffle_range(order->indices, 1, order->count, random_index,
                  random_context);
    order->position = 0;
}

static void begin_shuffled_cycle(Playback_Order *order, size_t current,
                                 Playback_Random_Index random_index,
                                 void *random_context)
{
    shuffle_range(order->indices, 0, order->count, random_index,
                  random_context);

    if (order->count > 1 && order->indices[0] == current) {
        size_t other =
            1 + random_offset(order->count - 1, random_index, random_context);
        order->indices[0] = order->indices[other];
        order->indices[other] = current;
    }

    order->position = 0;
}

void playback_order_init(Playback_Order *order)
{
    *order = (Playback_Order){0};
}

void playback_order_uninit(Playback_Order *order)
{
    free(order->indices);
    *order = (Playback_Order){0};
}

bool playback_order_reset(Playback_Order *order, size_t count, size_t current,
                          bool shuffled, Playback_Random_Index random_index,
                          void *random_context)
{
    if (count == 0) {
        playback_order_uninit(order);
        order->shuffled = shuffled;
        return true;
    }

    if (current >= count || count > SIZE_MAX / sizeof(*order->indices)) {
        return false;
    }

    size_t *indices = malloc(count * sizeof(*indices));

    if (indices == NULL) return false;

    free(order->indices);
    order->indices = indices;
    order->count = count;
    order->shuffled = shuffled;
    arrange_from_current(order, current, random_index, random_context);
    return true;
}

bool playback_order_set_shuffled(Playback_Order *order, size_t current,
                                 bool shuffled,
                                 Playback_Random_Index random_index,
                                 void *random_context)
{
    if (order->count == 0) {
        order->shuffled = shuffled;
        return true;
    }

    if (current >= order->count) return false;
    if (order->shuffled == shuffled) return true;

    order->shuffled = shuffled;
    arrange_from_current(order, current, random_index, random_context);
    return true;
}

bool playback_order_select(Playback_Order *order, size_t index,
                           Playback_Random_Index random_index,
                           void *random_context)
{
    if (index >= order->count) return false;

    arrange_from_current(order, index, random_index, random_context);
    return true;
}

bool playback_order_next(Playback_Order *order, bool repeat_all,
                         Playback_Random_Index random_index,
                         void *random_context, size_t *index)
{
    if (order->count == 0 || index == NULL) return false;

    if (order->position + 1 < order->count) {
        ++order->position;
    } else if (!repeat_all) {
        return false;
    } else if (order->shuffled) {
        size_t current = order->indices[order->position];
        begin_shuffled_cycle(order, current, random_index, random_context);
    } else {
        order->position = 0;
    }

    *index = order->indices[order->position];
    return true;
}

bool playback_order_previous(Playback_Order *order, bool repeat_all,
                             size_t *index)
{
    if (order->count == 0 || index == NULL) return false;

    if (order->position > 0) {
        --order->position;
    } else if (repeat_all && !order->shuffled && order->count > 1) {
        order->position = order->count - 1;
    } else {
        return false;
    }

    *index = order->indices[order->position];
    return true;
}

bool playback_order_can_next(const Playback_Order *order, bool repeat_all)
{
    if (order->count == 0) return false;

    return order->position + 1 < order->count ||
           (repeat_all && order->count > 1);
}

bool playback_order_can_previous(const Playback_Order *order, bool repeat_all)
{
    if (order->count == 0) return false;

    return order->position > 0 ||
           (repeat_all && !order->shuffled && order->count > 1);
}
