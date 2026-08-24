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
 * queue.h
 * - Queue implementation header file
 * ------------------------------------------------------------------------- */

#ifndef QUEUE_H
#define QUEUE_H

#include "app_audio.h"

struct Node
{
    uint8_t data[ENCODED_FRAME_LENGTH];
    Packet_State packet_state;
    uint8_t seq_num;
    uint8_t fragment_num;
    struct Node *next;
};

struct queue_t
{
    struct Node *front;
    struct Node *rear;
};

void QueueInsert(struct queue_t *queue, const uint8_t *x, Packet_State packet_state, uint8_t seq_num, uint8_t fragment_num);

void QueueFree(struct queue_t *queue);

void QueueInit(struct queue_t *queue);

uint8_t * QueueFront(struct queue_t *queue, Packet_State * packet_state, uint8_t * seq_num, uint8_t * fragment_num);

uint16_t QueueCount(struct queue_t *queue);

#endif    /* QUEUE_H */
