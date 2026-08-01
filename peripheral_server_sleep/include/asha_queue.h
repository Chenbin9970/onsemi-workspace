/* ----------------------------------------------------------------------------
 * asha_queue.h
 * ------------------------------------------------------------------------- */

#ifndef ASHA_QUEUE_H
#define ASHA_QUEUE_H

#include "asha_audio.h"

struct AshaNode {
    uint8_t data[ASHA_ENCODED_FRAME_LENGTH];
    AshaPacket_State packet_state;
    uint8_t seq_num;
    uint8_t fragment_num;
    struct AshaNode *next;
};

struct asha_queue_t {
    struct AshaNode *front;
    struct AshaNode *rear;
};

typedef struct asha_queue_t queue_t;

void     AshaQueueInit(queue_t *queue);
void     AshaQueueInsert(queue_t *queue, const uint8_t *x, AshaPacket_State pkt_state,
                         uint8_t seq_num, uint8_t frag_num);
void     AshaQueueFree(queue_t *queue);
uint8_t *AshaQueueFront(queue_t *queue, uint8_t *seq_num, AshaPacket_State *pkt_state,
                        uint8_t *frag_num);
uint16_t AshaQueueCount(queue_t *queue);

static inline void AshaQueueFlush(queue_t *queue)
{
    while (queue->front != NULL) AshaQueueFree(queue);
}

#endif /* ASHA_QUEUE_H */
