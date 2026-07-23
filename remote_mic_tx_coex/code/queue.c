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
 * -----------------------------------------------------------------------------
 */

#include <malloc.h>
#include <string.h>

#include "queue.h"

/* ----------------------------------------------------------------------------
 * Function      : void QueueInit(struct queue_t * queue, uint16_t x[])
 * ----------------------------------------------------------------------------
 * Description   : Initialize the queue
 * Inputs        : None
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void QueueInit(struct queue_t *queue)
{
    queue->front = NULL;
    queue->rear  = NULL;
}

/* ----------------------------------------------------------------------------
 * Function      : void QueueInsert(struct queue_t * queue, uint16_t x[])
 * ----------------------------------------------------------------------------
 * Description   : To insert one element into the queue
 * Inputs        : None
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void QueueInsert(struct queue_t *queue, uint16_t x[])
{
    struct Node *temp =
        (struct Node *)malloc(sizeof(struct Node));

    if (temp == NULL)
    {
        /* Memory allocation has been failed */
        return;
    }

    memcpy(temp->data, x, SUBFRAME_LENGTH * sizeof(uint16_t));
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
 * Inputs        : None
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
 * Function      : uint16_t * QueueFront(struct queue_t * queue)
 * ----------------------------------------------------------------------------
 * Description   : Get pointer to one element from the queue
 * Inputs        : None
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
uint16_t * QueueFront(struct queue_t *queue)
{
    if (queue->front == NULL)
    {
        return (NULL);
    }
    return (queue->front->data);
}
