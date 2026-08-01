/* ----------------------------------------------------------------------------
 * asha_queue.c
 * ------------------------------------------------------------------------- */

#include <malloc.h>
#include <string.h>
#include "asha_queue.h"

void AshaQueueInit(queue_t *queue)
{
    queue->front = NULL;
    queue->rear  = NULL;
}

void AshaQueueInsert(queue_t *queue, const uint8_t *x, AshaPacket_State pkt_state,
                     uint8_t seq_num, uint8_t frag_num)
{
    struct AshaNode *temp = (struct AshaNode *)malloc(sizeof(struct AshaNode));
    if (temp == NULL) return;

    memcpy(temp->data, x, ASHA_ENCODED_FRAME_LENGTH * sizeof(uint8_t));
    temp->packet_state = pkt_state;
    temp->seq_num      = seq_num;
    temp->fragment_num = frag_num;

    temp->next = NULL;
    if (queue->front == NULL && queue->rear == NULL)
    {
        queue->front = queue->rear = temp;
        return;
    }
    queue->rear->next = temp;
    queue->rear = temp;
}

void AshaQueueFree(queue_t *queue)
{
    struct AshaNode *temp = queue->front;
    if (queue->front == NULL) return;
    if (queue->front == queue->rear)
        queue->front = queue->rear = NULL;
    else
        queue->front = queue->front->next;
    free(temp);
}

uint8_t *AshaQueueFront(queue_t *queue, uint8_t *seq_num, AshaPacket_State *pkt_state,
                        uint8_t *frag_num)
{
    if (queue->front == NULL) return NULL;
    *pkt_state = queue->front->packet_state;
    *seq_num   = queue->front->seq_num;
    *frag_num  = queue->front->fragment_num;
    return queue->front->data;
}

uint16_t AshaQueueCount(queue_t *queue)
{
    struct AshaNode *temp = queue->front;
    uint16_t cnt = 0;
    while (temp != NULL)
    {
        cnt++;
        if (temp->next == NULL) break;
        temp = temp->next;
    }
    return cnt;
}
