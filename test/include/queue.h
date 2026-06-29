/* ----------------------------------------------------------------------------
 * Copyright (c) 2015-2017 Semiconductor Components Industries, LLC (d/b/a
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
 * - Simple queue implementation header file
 * ----------------------------------------------------------------------------
 * $Revision: 1.5 $
 * $Date: 2018/02/27 15:46:06 $
 * ------------------------------------------------------------------------- */

#ifndef QUEUE_H_
#define QUEUE_H_
/* ----------------------------------------------------------------------------
 * If building with a C++ compiler, make all of the definitions in this header
 * have a C binding.
 * ------------------------------------------------------------------------- */
#ifdef __cplusplus
extern "C"
{
#endif    /* ifdef __cplusplus */

#include "app.h"

struct Node
{
    uint16_t data[SUBFRAME_LENGTH];
    struct Node *next;
};

struct queue_t
{
    struct Node *front;
    struct Node *rear;
};

void QueueInsert(struct queue_t *queue, uint16_t x[]);

void QueueFree(struct queue_t *queue);

void QueueInit(struct queue_t *queue);

uint16_t * QueueFront(struct queue_t *queue);

/* ----------------------------------------------------------------------------
 * Close the 'extern "C"' block
 * ------------------------------------------------------------------------- */
#ifdef __cplusplus
}
#endif    /* ifdef __cplusplus */
#endif    /* QUEUE_H_ */
