/* ----------------------------------------------------------------------------
 * Copyright (c) 2017 Semiconductor Components Industries, LLC (d/b/a
 * ON Semiconductor), All Rights Reserved
 *
 * This code is the property of ON Semiconductor and may not be redistributed
 * in any form without prior written permission from ON Semiconductor.
 * The terms of use and warranty for this code are covered by contractual
 * agreements between ON Semiconductor and the licensee.
 *
 * This is Reusable Code.
 *
 * ----------------------------------------------------------------------------
 * Queue.c
 * - Queue implementation
 * ------------------------------------------------------------------------- */

#include <malloc.h>
#include <string.h>
#include "queue.h"

/* ----------------------------------------------------------------------------
 * Function      : void QueueInit(struct queue_t * queue)
 * ----------------------------------------------------------------------------
 * Description   : Initialize the queue
 * Inputs        : - queue: Queue pointer
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void QueueInit(struct queue_t *queue)
{
    queue->front = NULL;
    queue->rear  = NULL;
}

/* ----------------------------------------------------------------------------
 * Function      : void QueueInsert(struct queue_t *queue, const uint8_t *x, Packet_State packet_state, uint8_t seq_num, uint8_t fragment_num)
 * ----------------------------------------------------------------------------
 * Description   : To insert one element into the queue
 * Inputs        : - queue:        Queue pointer
 *                 - x:            Input data for the element
 *                 - packet_state: State of the packet
 *                 - seq_num:      Packet sequence number from host
 *                 - fragment_num: Frame number
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void QueueInsert(struct queue_t *queue, const uint8_t *x, Packet_State packet_state, uint8_t seq_num, uint8_t fragment_num)
{
    struct Node *temp =
        (struct Node *)malloc(sizeof(struct Node));

    if (temp == NULL)
    {
        /* Memory allocation has been failed */
        return;
    }

    memcpy(temp->data, x, ENCODED_FRAME_LENGTH * sizeof(uint8_t));
    temp->packet_state = packet_state;
    temp->seq_num = seq_num;
    temp->fragment_num = fragment_num;

    temp->next = NULL;
    if (queue->front == NULL && queue->rear == NULL)
    {
        queue->front = queue->rear = temp;
        return;
    }

    queue->rear->next = temp;
    queue->rear = temp;
}

/* ----------------------------------------------------------------------------
 * Function      : void QueueFree(struct queue_t * queue)
 * ----------------------------------------------------------------------------
 * Description   : Free one element from the queue
 * Inputs        : - queue: Queue pointer
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void QueueFree(struct queue_t *queue)
{
    struct Node *temp = queue->front;
    if (queue->front == NULL)
    {
        return;
    }
    if (queue->front == queue->rear)
    {
        queue->front = queue->rear = NULL;
    }
    else
    {
        queue->front = queue->front->next;
    }
    free(temp);
}

/* ----------------------------------------------------------------------------
 * Function      : uint8_t * QueueFront(struct queue_t *queue, Packet_State * packet_state, uint8_t * seq_num, uint8_t * fragment_num)
 * ----------------------------------------------------------------------------
 * Description   : Get pointer to one element from the queue
 * Inputs        : - queue:        Queue pointer
 *                 - packet_state: State of the packet
 *                 - seq_num:      Packet sequence number from host
 *                 - fragment_num: Frame number
 *
 * Outputs       : return: Pointer to the front element
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
uint8_t * QueueFront(struct queue_t *queue, Packet_State * packet_state, uint8_t * seq_num, uint8_t * fragment_num)
{
    if (queue->front == NULL)
    {
        return (NULL);
    }

    *packet_state = queue->front->packet_state;
    *seq_num = queue->front->seq_num;
    *fragment_num = queue->front->fragment_num;
    return (queue->front->data);
}

/* ----------------------------------------------------------------------------
 * Function      : uint16_t QueueCount(struct queue_t *queue)
 * ----------------------------------------------------------------------------
 * Description   : Counts number of elements in the queue
 * Inputs        : - queue: Queue pointer
 * Outputs       : return: Number of elements
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
uint16_t QueueCount(struct queue_t *queue)
{
    struct Node *temp = queue->front;
    uint16_t element_cnt = 0;

    while (temp != NULL)
    {
        element_cnt ++;
        if (temp->next == NULL)
            break;
        temp = temp->next;
    }

    return element_cnt;
}
