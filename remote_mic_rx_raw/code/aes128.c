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
 * aes128.c
 * This is an implementation of the AES128 algorithm as described by NIST
 * (FIPS-197).
 * ----------------------------------------------------------------------------
 * $Revision: 1.1 $
 * $Date: 2018/08/29 19:16:29 $
 * ------------------------------------------------------------------------- */
#include <rsl10.h>
#include <string.h>
#include <stdlib.h>
#include "aes128.h"

/* Define way of computing multiplication */
#define AES_LOOKUP_TABLE

/* forward sbox */
static const uint8_t sbox[256] =
{
    /* 0     1     2     3     4     5     6     7     8     9     A     B     C     D     E     F */
    0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76,
    0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0,
    0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
    0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75,
    0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0, 0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
    0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
    0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
    0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2,
    0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
    0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
    0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c, 0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
    0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
    0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
    0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
    0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
    0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16
};

/* inverse sbox */
static const uint8_t rsbox[256] =
{
    /* 0     1     2     3     4     5     6     7     8     9     A     B     C     D     E     F */
    0x52, 0x09, 0x6a, 0xd5, 0x30, 0x36, 0xa5, 0x38, 0xbf, 0x40, 0xa3, 0x9e, 0x81, 0xf3, 0xd7, 0xfb,
    0x7c, 0xe3, 0x39, 0x82, 0x9b, 0x2f, 0xff, 0x87, 0x34, 0x8e, 0x43, 0x44, 0xc4, 0xde, 0xe9, 0xcb,
    0x54, 0x7b, 0x94, 0x32, 0xa6, 0xc2, 0x23, 0x3d, 0xee, 0x4c, 0x95, 0x0b, 0x42, 0xfa, 0xc3, 0x4e,
    0x08, 0x2e, 0xa1, 0x66, 0x28, 0xd9, 0x24, 0xb2, 0x76, 0x5b, 0xa2, 0x49, 0x6d, 0x8b, 0xd1, 0x25,
    0x72, 0xf8, 0xf6, 0x64, 0x86, 0x68, 0x98, 0x16, 0xd4, 0xa4, 0x5c, 0xcc, 0x5d, 0x65, 0xb6, 0x92,
    0x6c, 0x70, 0x48, 0x50, 0xfd, 0xed, 0xb9, 0xda, 0x5e, 0x15, 0x46, 0x57, 0xa7, 0x8d, 0x9d, 0x84,
    0x90, 0xd8, 0xab, 0x00, 0x8c, 0xbc, 0xd3, 0x0a, 0xf7, 0xe4, 0x58, 0x05, 0xb8, 0xb3, 0x45, 0x06,
    0xd0, 0x2c, 0x1e, 0x8f, 0xca, 0x3f, 0x0f, 0x02, 0xc1, 0xaf, 0xbd, 0x03, 0x01, 0x13, 0x8a, 0x6b,
    0x3a, 0x91, 0x11, 0x41, 0x4f, 0x67, 0xdc, 0xea, 0x97, 0xf2, 0xcf, 0xce, 0xf0, 0xb4, 0xe6, 0x73,
    0x96, 0xac, 0x74, 0x22, 0xe7, 0xad, 0x35, 0x85, 0xe2, 0xf9, 0x37, 0xe8, 0x1c, 0x75, 0xdf, 0x6e,
    0x47, 0xf1, 0x1a, 0x71, 0x1d, 0x29, 0xc5, 0x89, 0x6f, 0xb7, 0x62, 0x0e, 0xaa, 0x18, 0xbe, 0x1b,
    0xfc, 0x56, 0x3e, 0x4b, 0xc6, 0xd2, 0x79, 0x20, 0x9a, 0xdb, 0xc0, 0xfe, 0x78, 0xcd, 0x5a, 0xf4,
    0x1f, 0xdd, 0xa8, 0x33, 0x88, 0x07, 0xc7, 0x31, 0xb1, 0x12, 0x10, 0x59, 0x27, 0x80, 0xec, 0x5f,
    0x60, 0x51, 0x7f, 0xa9, 0x19, 0xb5, 0x4a, 0x0d, 0x2d, 0xe5, 0x7a, 0x9f, 0x93, 0xc9, 0x9c, 0xef,
    0xa0, 0xe0, 0x3b, 0x4d, 0xae, 0x2a, 0xf5, 0xb0, 0xc8, 0xeb, 0xbb, 0x3c, 0x83, 0x53, 0x99, 0x61,
    0x17, 0x2b, 0x04, 0x7e, 0xba, 0x77, 0xd6, 0x26, 0xe1, 0x69, 0x14, 0x63, 0x55, 0x21, 0x0c, 0x7d
};

#if defined (AES_LOOKUP_TABLE)
static const uint8_t xtime2[256] = {
    /* 0     1     2     3     4     5     6     7     8     9     A     B     C     D     E     F */
    0x00, 0x02, 0x04, 0x06, 0x08, 0x0a, 0x0c, 0x0e, 0x10, 0x12, 0x14, 0x16, 0x18, 0x1a, 0x1c, 0x1e,
    0x20, 0x22, 0x24, 0x26, 0x28, 0x2a, 0x2c, 0x2e, 0x30, 0x32, 0x34, 0x36, 0x38, 0x3a, 0x3c, 0x3e,
    0x40, 0x42, 0x44, 0x46, 0x48, 0x4a, 0x4c, 0x4e, 0x50, 0x52, 0x54, 0x56, 0x58, 0x5a, 0x5c, 0x5e,
    0x60, 0x62, 0x64, 0x66, 0x68, 0x6a, 0x6c, 0x6e, 0x70, 0x72, 0x74, 0x76, 0x78, 0x7a, 0x7c, 0x7e,
    0x80, 0x82, 0x84, 0x86, 0x88, 0x8a, 0x8c, 0x8e, 0x90, 0x92, 0x94, 0x96, 0x98, 0x9a, 0x9c, 0x9e,
    0xa0, 0xa2, 0xa4, 0xa6, 0xa8, 0xaa, 0xac, 0xae, 0xb0, 0xb2, 0xb4, 0xb6, 0xb8, 0xba, 0xbc, 0xbe,
    0xc0, 0xc2, 0xc4, 0xc6, 0xc8, 0xca, 0xcc, 0xce, 0xd0, 0xd2, 0xd4, 0xd6, 0xd8, 0xda, 0xdc, 0xde,
    0xe0, 0xe2, 0xe4, 0xe6, 0xe8, 0xea, 0xec, 0xee, 0xf0, 0xf2, 0xf4, 0xf6, 0xf8, 0xfa, 0xfc, 0xfe,
    0x1b, 0x19, 0x1f, 0x1d, 0x13, 0x11, 0x17, 0x15, 0x0b, 0x09, 0x0f, 0x0d, 0x03, 0x01, 0x07, 0x05,
    0x3b, 0x39, 0x3f, 0x3d, 0x33, 0x31, 0x37, 0x35, 0x2b, 0x29, 0x2f, 0x2d, 0x23, 0x21, 0x27, 0x25,
    0x5b, 0x59, 0x5f, 0x5d, 0x53, 0x51, 0x57, 0x55, 0x4b, 0x49, 0x4f, 0x4d, 0x43, 0x41, 0x47, 0x45,
    0x7b, 0x79, 0x7f, 0x7d, 0x73, 0x71, 0x77, 0x75, 0x6b, 0x69, 0x6f, 0x6d, 0x63, 0x61, 0x67, 0x65,
    0x9b, 0x99, 0x9f, 0x9d, 0x93, 0x91, 0x97, 0x95, 0x8b, 0x89, 0x8f, 0x8d, 0x83, 0x81, 0x87, 0x85,
    0xbb, 0xb9, 0xbf, 0xbd, 0xb3, 0xb1, 0xb7, 0xb5, 0xab, 0xa9, 0xaf, 0xad, 0xa3, 0xa1, 0xa7, 0xa5,
    0xdb, 0xd9, 0xdf, 0xdd, 0xd3, 0xd1, 0xd7, 0xd5, 0xcb, 0xc9, 0xcf, 0xcd, 0xc3, 0xc1, 0xc7, 0xc5,
    0xfb, 0xf9, 0xff, 0xfd, 0xf3, 0xf1, 0xf7, 0xf5, 0xeb, 0xe9, 0xef, 0xed, 0xe3, 0xe1, 0xe7, 0xe5
};

static const uint8_t xtime9[256] = {
    /* 0     1     2     3     4     5     6     7     8     9     A     B     C     D     E     F */
    0x00, 0x09, 0x12, 0x1b, 0x24, 0x2d, 0x36, 0x3f, 0x48, 0x41, 0x5a, 0x53, 0x6c, 0x65, 0x7e, 0x77,
    0x90, 0x99, 0x82, 0x8b, 0xb4, 0xbd, 0xa6, 0xaf, 0xd8, 0xd1, 0xca, 0xc3, 0xfc, 0xf5, 0xee, 0xe7,
    0x3b, 0x32, 0x29, 0x20, 0x1f, 0x16, 0x0d, 0x04, 0x73, 0x7a, 0x61, 0x68, 0x57, 0x5e, 0x45, 0x4c,
    0xab, 0xa2, 0xb9, 0xb0, 0x8f, 0x86, 0x9d, 0x94, 0xe3, 0xea, 0xf1, 0xf8, 0xc7, 0xce, 0xd5, 0xdc,
    0x76, 0x7f, 0x64, 0x6d, 0x52, 0x5b, 0x40, 0x49, 0x3e, 0x37, 0x2c, 0x25, 0x1a, 0x13, 0x08, 0x01,
    0xe6, 0xef, 0xf4, 0xfd, 0xc2, 0xcb, 0xd0, 0xd9, 0xae, 0xa7, 0xbc, 0xb5, 0x8a, 0x83, 0x98, 0x91,
    0x4d, 0x44, 0x5f, 0x56, 0x69, 0x60, 0x7b, 0x72, 0x05, 0x0c, 0x17, 0x1e, 0x21, 0x28, 0x33, 0x3a,
    0xdd, 0xd4, 0xcf, 0xc6, 0xf9, 0xf0, 0xeb, 0xe2, 0x95, 0x9c, 0x87, 0x8e, 0xb1, 0xb8, 0xa3, 0xaa,
    0xec, 0xe5, 0xfe, 0xf7, 0xc8, 0xc1, 0xda, 0xd3, 0xa4, 0xad, 0xb6, 0xbf, 0x80, 0x89, 0x92, 0x9b,
    0x7c, 0x75, 0x6e, 0x67, 0x58, 0x51, 0x4a, 0x43, 0x34, 0x3d, 0x26, 0x2f, 0x10, 0x19, 0x02, 0x0b,
    0xd7, 0xde, 0xc5, 0xcc, 0xf3, 0xfa, 0xe1, 0xe8, 0x9f, 0x96, 0x8d, 0x84, 0xbb, 0xb2, 0xa9, 0xa0,
    0x47, 0x4e, 0x55, 0x5c, 0x63, 0x6a, 0x71, 0x78, 0x0f, 0x06, 0x1d, 0x14, 0x2b, 0x22, 0x39, 0x30,
    0x9a, 0x93, 0x88, 0x81, 0xbe, 0xb7, 0xac, 0xa5, 0xd2, 0xdb, 0xc0, 0xc9, 0xf6, 0xff, 0xe4, 0xed,
    0x0a, 0x03, 0x18, 0x11, 0x2e, 0x27, 0x3c, 0x35, 0x42, 0x4b, 0x50, 0x59, 0x66, 0x6f, 0x74, 0x7d,
    0xa1, 0xa8, 0xb3, 0xba, 0x85, 0x8c, 0x97, 0x9e, 0xe9, 0xe0, 0xfb, 0xf2, 0xcd, 0xc4, 0xdf, 0xd6,
    0x31, 0x38, 0x23, 0x2a, 0x15, 0x1c, 0x07, 0x0e, 0x79, 0x70, 0x6b, 0x62, 0x5d, 0x54, 0x4f, 0x46
};

static const uint8_t xtime11[256] = {
    /* 0     1     2     3     4     5     6     7     8     9     A     B     C     D     E     F */
    0x00, 0x0b, 0x16, 0x1d, 0x2c, 0x27, 0x3a, 0x31, 0x58, 0x53, 0x4e, 0x45, 0x74, 0x7f, 0x62, 0x69,
    0xb0, 0xbb, 0xa6, 0xad, 0x9c, 0x97, 0x8a, 0x81, 0xe8, 0xe3, 0xfe, 0xf5, 0xc4, 0xcf, 0xd2, 0xd9,
    0x7b, 0x70, 0x6d, 0x66, 0x57, 0x5c, 0x41, 0x4a, 0x23, 0x28, 0x35, 0x3e, 0x0f, 0x04, 0x19, 0x12,
    0xcb, 0xc0, 0xdd, 0xd6, 0xe7, 0xec, 0xf1, 0xfa, 0x93, 0x98, 0x85, 0x8e, 0xbf, 0xb4, 0xa9, 0xa2,
    0xf6, 0xfd, 0xe0, 0xeb, 0xda, 0xd1, 0xcc, 0xc7, 0xae, 0xa5, 0xb8, 0xb3, 0x82, 0x89, 0x94, 0x9f,
    0x46, 0x4d, 0x50, 0x5b, 0x6a, 0x61, 0x7c, 0x77, 0x1e, 0x15, 0x08, 0x03, 0x32, 0x39, 0x24, 0x2f,
    0x8d, 0x86, 0x9b, 0x90, 0xa1, 0xaa, 0xb7, 0xbc, 0xd5, 0xde, 0xc3, 0xc8, 0xf9, 0xf2, 0xef, 0xe4,
    0x3d, 0x36, 0x2b, 0x20, 0x11, 0x1a, 0x07, 0x0c, 0x65, 0x6e, 0x73, 0x78, 0x49, 0x42, 0x5f, 0x54,
    0xf7, 0xfc, 0xe1, 0xea, 0xdb, 0xd0, 0xcd, 0xc6, 0xaf, 0xa4, 0xb9, 0xb2, 0x83, 0x88, 0x95, 0x9e,
    0x47, 0x4c, 0x51, 0x5a, 0x6b, 0x60, 0x7d, 0x76, 0x1f, 0x14, 0x09, 0x02, 0x33, 0x38, 0x25, 0x2e,
    0x8c, 0x87, 0x9a, 0x91, 0xa0, 0xab, 0xb6, 0xbd, 0xd4, 0xdf, 0xc2, 0xc9, 0xf8, 0xf3, 0xee, 0xe5,
    0x3c, 0x37, 0x2a, 0x21, 0x10, 0x1b, 0x06, 0x0d, 0x64, 0x6f, 0x72, 0x79, 0x48, 0x43, 0x5e, 0x55,
    0x01, 0x0a, 0x17, 0x1c, 0x2d, 0x26, 0x3b, 0x30, 0x59, 0x52, 0x4f, 0x44, 0x75, 0x7e, 0x63, 0x68,
    0xb1, 0xba, 0xa7, 0xac, 0x9d, 0x96, 0x8b, 0x80, 0xe9, 0xe2, 0xff, 0xf4, 0xc5, 0xce, 0xd3, 0xd8,
    0x7a, 0x71, 0x6c, 0x67, 0x56, 0x5d, 0x40, 0x4b, 0x22, 0x29, 0x34, 0x3f, 0x0e, 0x05, 0x18, 0x13,
    0xca, 0xc1, 0xdc, 0xd7, 0xe6, 0xed, 0xf0, 0xfb, 0x92, 0x99, 0x84, 0x8f, 0xbe, 0xb5, 0xa8, 0xa3
};

static const uint8_t xtime13[256] = {
    /* 0     1     2     3     4     5     6     7     8     9     A     B     C     D     E     F */
    0x00, 0x0d, 0x1a, 0x17, 0x34, 0x39, 0x2e, 0x23, 0x68, 0x65, 0x72, 0x7f, 0x5c, 0x51, 0x46, 0x4b,
    0xd0, 0xdd, 0xca, 0xc7, 0xe4, 0xe9, 0xfe, 0xf3, 0xb8, 0xb5, 0xa2, 0xaf, 0x8c, 0x81, 0x96, 0x9b,
    0xbb, 0xb6, 0xa1, 0xac, 0x8f, 0x82, 0x95, 0x98, 0xd3, 0xde, 0xc9, 0xc4, 0xe7, 0xea, 0xfd, 0xf0,
    0x6b, 0x66, 0x71, 0x7c, 0x5f, 0x52, 0x45, 0x48, 0x03, 0x0e, 0x19, 0x14, 0x37, 0x3a, 0x2d, 0x20,
    0x6d, 0x60, 0x77, 0x7a, 0x59, 0x54, 0x43, 0x4e, 0x05, 0x08, 0x1f, 0x12, 0x31, 0x3c, 0x2b, 0x26,
    0xbd, 0xb0, 0xa7, 0xaa, 0x89, 0x84, 0x93, 0x9e, 0xd5, 0xd8, 0xcf, 0xc2, 0xe1, 0xec, 0xfb, 0xf6,
    0xd6, 0xdb, 0xcc, 0xc1, 0xe2, 0xef, 0xf8, 0xf5, 0xbe, 0xb3, 0xa4, 0xa9, 0x8a, 0x87, 0x90, 0x9d,
    0x06, 0x0b, 0x1c, 0x11, 0x32, 0x3f, 0x28, 0x25, 0x6e, 0x63, 0x74, 0x79, 0x5a, 0x57, 0x40, 0x4d,
    0xda, 0xd7, 0xc0, 0xcd, 0xee, 0xe3, 0xf4, 0xf9, 0xb2, 0xbf, 0xa8, 0xa5, 0x86, 0x8b, 0x9c, 0x91,
    0x0a, 0x07, 0x10, 0x1d, 0x3e, 0x33, 0x24, 0x29, 0x62, 0x6f, 0x78, 0x75, 0x56, 0x5b, 0x4c, 0x41,
    0x61, 0x6c, 0x7b, 0x76, 0x55, 0x58, 0x4f, 0x42, 0x09, 0x04, 0x13, 0x1e, 0x3d, 0x30, 0x27, 0x2a,
    0xb1, 0xbc, 0xab, 0xa6, 0x85, 0x88, 0x9f, 0x92, 0xd9, 0xd4, 0xc3, 0xce, 0xed, 0xe0, 0xf7, 0xfa,
    0xb7, 0xba, 0xad, 0xa0, 0x83, 0x8e, 0x99, 0x94, 0xdf, 0xd2, 0xc5, 0xc8, 0xeb, 0xe6, 0xf1, 0xfc,
    0x67, 0x6a, 0x7d, 0x70, 0x53, 0x5e, 0x49, 0x44, 0x0f, 0x02, 0x15, 0x18, 0x3b, 0x36, 0x21, 0x2c,
    0x0c, 0x01, 0x16, 0x1b, 0x38, 0x35, 0x22, 0x2f, 0x64, 0x69, 0x7e, 0x73, 0x50, 0x5d, 0x4a, 0x47,
    0xdc, 0xd1, 0xc6, 0xcb, 0xe8, 0xe5, 0xf2, 0xff, 0xb4, 0xb9, 0xae, 0xa3, 0x80, 0x8d, 0x9a, 0x97
};

static const uint8_t xtime14[256] = {
    /* 0     1     2     3     4     5     6     7     8     9     A     B     C     D     E     F */
    0x00, 0x0e, 0x1c, 0x12, 0x38, 0x36, 0x24, 0x2a, 0x70, 0x7e, 0x6c, 0x62, 0x48, 0x46, 0x54, 0x5a,
    0xe0, 0xee, 0xfc, 0xf2, 0xd8, 0xd6, 0xc4, 0xca, 0x90, 0x9e, 0x8c, 0x82, 0xa8, 0xa6, 0xb4, 0xba,
    0xdb, 0xd5, 0xc7, 0xc9, 0xe3, 0xed, 0xff, 0xf1, 0xab, 0xa5, 0xb7, 0xb9, 0x93, 0x9d, 0x8f, 0x81,
    0x3b, 0x35, 0x27, 0x29, 0x03, 0x0d, 0x1f, 0x11, 0x4b, 0x45, 0x57, 0x59, 0x73, 0x7d, 0x6f, 0x61,
    0xad, 0xa3, 0xb1, 0xbf, 0x95, 0x9b, 0x89, 0x87, 0xdd, 0xd3, 0xc1, 0xcf, 0xe5, 0xeb, 0xf9, 0xf7,
    0x4d, 0x43, 0x51, 0x5f, 0x75, 0x7b, 0x69, 0x67, 0x3d, 0x33, 0x21, 0x2f, 0x05, 0x0b, 0x19, 0x17,
    0x76, 0x78, 0x6a, 0x64, 0x4e, 0x40, 0x52, 0x5c, 0x06, 0x08, 0x1a, 0x14, 0x3e, 0x30, 0x22, 0x2c,
    0x96, 0x98, 0x8a, 0x84, 0xae, 0xa0, 0xb2, 0xbc, 0xe6, 0xe8, 0xfa, 0xf4, 0xde, 0xd0, 0xc2, 0xcc,
    0x41, 0x4f, 0x5d, 0x53, 0x79, 0x77, 0x65, 0x6b, 0x31, 0x3f, 0x2d, 0x23, 0x09, 0x07, 0x15, 0x1b,
    0xa1, 0xaf, 0xbd, 0xb3, 0x99, 0x97, 0x85, 0x8b, 0xd1, 0xdf, 0xcd, 0xc3, 0xe9, 0xe7, 0xf5, 0xfb,
    0x9a, 0x94, 0x86, 0x88, 0xa2, 0xac, 0xbe, 0xb0, 0xea, 0xe4, 0xf6, 0xf8, 0xd2, 0xdc, 0xce, 0xc0,
    0x7a, 0x74, 0x66, 0x68, 0x42, 0x4c, 0x5e, 0x50, 0x0a, 0x04, 0x16, 0x18, 0x32, 0x3c, 0x2e, 0x20,
    0xec, 0xe2, 0xf0, 0xfe, 0xd4, 0xda, 0xc8, 0xc6, 0x9c, 0x92, 0x80, 0x8e, 0xa4, 0xaa, 0xb8, 0xb6,
    0x0c, 0x02, 0x10, 0x1e, 0x34, 0x3a, 0x28, 0x26, 0x7c, 0x72, 0x60, 0x6e, 0x44, 0x4a, 0x58, 0x56,
    0x37, 0x39, 0x2b, 0x25, 0x0f, 0x01, 0x13, 0x1d, 0x47, 0x49, 0x5b, 0x55, 0x7f, 0x71, 0x63, 0x6d,
    0xd7, 0xd9, 0xcb, 0xc5, 0xef, 0xe1, 0xf3, 0xfd, 0xa7, 0xa9, 0xbb, 0xb5, 0x9f, 0x91, 0x83, 0x8d
};
#endif    /* if defined (AES_LOOKUP_TABLE) */

/* ----------------------------------------------------------------------------
 * Forward declaration
 * ------------------------------------------------------------------------- */
#if !defined (AES_LOOKUP_TABLE)
static uint8_t xtime(uint8_t x);

#endif    /* if !defined (AES_LOOKUP_TABLE) */
static uint8_t gmult(uint8_t value, uint8_t multiplier);

static void add_round_key(uint8_t *const round_key, uint8_t *state);

static void sub_bytes_shift_rows(uint8_t *state);

static void inv_shift_rows_inv_sub_bytes(uint8_t *state);

static void mix_columns(uint8_t *state);

static void inv_mix_columns(uint8_t *state);

#if !defined (AES_LOOKUP_TABLE)
/* ----------------------------------------------------------------------------
 * Function      : uint8_t xtime(uint8_t x)
 * ----------------------------------------------------------------------------
 * Description   : multiplication in the Galois field
 * Inputs        : x - multiply by x
 * Outputs       : result of the multiplication
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
static uint8_t xtime(uint8_t x)
{
    return ((x << 1) ^ (((x >> 7) & 1) * 0x1b));
}

#endif    /* if !defined (AES_LOOKUP_TABLE) */

/* ----------------------------------------------------------------------------
 * Function      : uint8_t gmult(uint8_t value,  uint8_t multiplier)
 * ----------------------------------------------------------------------------
 * Description   : This function multiplies by multiplier in the galois field
 * Inputs        : value      - multiplicand
 *                 multiplier - multiplier
 * Outputs       : result of the multiplication
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
static uint8_t gmult(uint8_t value, uint8_t multiplier)
{
    switch (multiplier)
    {
#if defined (AES_LOOKUP_TABLE)

        /* ----------------------------------------------------------------- */
        case 2:
        {
            return (xtime2[value]);

            /* -----------------------------------------------------------------
             * */
            /* Multiplied by 3 in Rijndael's Galois field is replaced by 2*x ^ x
             * */
        }

        case 3:
        {
            return (xtime2[value] ^ value);
        }
        break;

        /* ----------------------------------------------------------------- */
        case 9:
        {
            return (xtime9[value]);
        }
        break;

        /* ----------------------------------------------------------------- */
        case 11:
        {
            return (xtime11[value]);
        }
        break;

        /* ----------------------------------------------------------------- */
        case 13:
        {
            return (xtime13[value]);
        }
        break;

        /* ----------------------------------------------------------------- */
        case 14:
        {
            return (xtime14[value]);
        }
        break;

        /* ----------------------------------------------------------------- */
        default:
        {
            return (0);
        }
        break;
#else    /* if defined (AES_LOOKUP_TABLE) */

        /* ----------------------------------------------------------------- */
        case 2:
        {
            return (xtime(value));
        }
        break;

        /* ----------------------------------------------------------------- */
        /* Multiplied by 3 in Rijndael's Galois field is replaced by 2*x ^ x */
        case 3:
        {
            return (xtime(value) ^ value);
        }
        break;

        /* ----------------------------------------------------------------- */
        case 9:
        case 11:
        case 13:
        case 14:
        {
            return (((multiplier      & 1) * value) ^
                    ((multiplier >> 1 & 1) * xtime(value)) ^
                    ((multiplier >> 2 & 1) * xtime(xtime(value))) ^
                    ((multiplier >> 3 & 1) * xtime(xtime(xtime(value)))) ^
                    ((multiplier >> 4 & 1) *
                     xtime(xtime(xtime(xtime(value))))));
        }
        break;

        /* ----------------------------------------------------------------- */
        default:
        {
            return (0);
        }
        break;
#endif    /* if defined (AES_LOOKUP_TABLE) */
    }
}

/* ----------------------------------------------------------------------------
 * Function      : void aes128_key_expand(uint8_t *round_key)
 * ----------------------------------------------------------------------------
 * Description   : This function produces round keys.
 *                 The round keys are used in each round to decrypt the states.
 * Inputs        : round_key - pointer to the expanded keys
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void aes128_key_expand(uint8_t *round_key)
{
    /* Note: As index starts at 1 , the 1st table element is not used */
    const uint8_t rcon[AES128_ROUNDS + 1] =
    {
        0x8d, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36
    };

    /* Used for the column/row operations */
    uint8_t tmp[4];
    uint8_t k;

    /* All other round keys are found from the previous round keys. */
    for (uint32_t i = 4; (i < ((AES128_ROUNDS + 1) * 4)); i++)
    {
        /* load previous round key */
        tmp[0] = round_key[(i - 1) * 4 + 0];
        tmp[1] = round_key[(i - 1) * 4 + 1];
        tmp[2] = round_key[(i - 1) * 4 + 2];
        tmp[3] = round_key[(i - 1) * 4 + 3];

        if (i % 4 == 0)
        {
            /* RotWord(): rotates the 4 bytes in a word to the left once.
             * [a0,a1,a2,a3] becomes [a1,a2,a3,a0] */
            k = tmp[0];
            tmp[0]  = tmp[1];
            tmp[1]  = tmp[2];
            tmp[2]  = tmp[3];
            tmp[3]  = k;

            /*SubWord(): takes a four-byte input word and applies the S-box
             * to each of the four bytes to produce an output word. */
            tmp[0]  = sbox[tmp[0]];
            tmp[1]  = sbox[tmp[1]];
            tmp[2]  = sbox[tmp[2]];
            tmp[3]  = sbox[tmp[3]];

            tmp[0] ^= rcon[i / 4];
        }
        round_key[i * 4 + 0] = round_key[(i - 4) * 4 + 0] ^ tmp[0];
        round_key[i * 4 + 1] = round_key[(i - 4) * 4 + 1] ^ tmp[1];
        round_key[i * 4 + 2] = round_key[(i - 4) * 4 + 2] ^ tmp[2];
        round_key[i * 4 + 3] = round_key[(i - 4) * 4 + 3] ^ tmp[3];
    }
}

/* ----------------------------------------------------------------------------
 * Function      : void add_round_key(uint8_t *round_key, uint8_t *state)
 * ----------------------------------------------------------------------------
 * Description   : This function adds the round key to state.
 *                 The round key is added to the state by an XOR function.
 * Inputs        : round_key - pointer to the expanded keys
 *                 state     - matrix to add round key
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
static void add_round_key(uint8_t *const round_key, uint8_t *state)
{
    /* --------------------------------------------------------------------- */
    /* column 0 */
    /* --------------------------------------------------------------------- */
    state[0] ^= round_key[0];
    state[4] ^= round_key[4];
    state[8] ^= round_key[8];
    state[12] ^= round_key[12];

    /* --------------------------------------------------------------------- */
    /* column 1 */
    /* --------------------------------------------------------------------- */
    state[1] ^= round_key[1];
    state[5] ^= round_key[5];
    state[9] ^= round_key[9];
    state[13] ^= round_key[13];

    /* --------------------------------------------------------------------- */
    /* column 2 */
    /* --------------------------------------------------------------------- */
    state[2] ^= round_key[2];
    state[6] ^= round_key[6];
    state[10] ^= round_key[10];
    state[14] ^= round_key[14];

    /* --------------------------------------------------------------------- */
    /* column 3 */
    /* --------------------------------------------------------------------- */
    state[3] ^= round_key[3];
    state[7] ^= round_key[7];
    state[11] ^= round_key[11];
    state[15] ^= round_key[15];
}

/* ----------------------------------------------------------------------------
 * Function      : void sub_bytes_shift_rows(uint8_t *state)
 * ----------------------------------------------------------------------------
 * Description   : This function substitutes (sbox) the values in the state and
 *                 shifts rows to left.
 * Inputs        : state - pointer to array holding the intermediate results.
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
static void sub_bytes_shift_rows(uint8_t *state)
{
    uint8_t tmp;

    /* row 0: No Rotation, just get the S-Box value */
    state[0] = sbox[state[0]];
    state[4] = sbox[state[4]];
    state[8] = sbox[state[8]];
    state[12] = sbox[state[12]];

    /* row 1: rotate to left (1x) */
    tmp = sbox[state[1]];
    state[1] = sbox[state[5]];
    state[5] = sbox[state[9]];
    state[9] = sbox[state[13]];
    state[13] = tmp;

    /* row 2: rotate to left (2x) */
    tmp = sbox[state[2]];
    state[2] = sbox[state[10]];
    state[10] = tmp;

    tmp = sbox[state[6]];
    state[6] = sbox[state[14]];
    state[14] = tmp;

    /* row 3: rotate to left (3x) */
    tmp = sbox[state[15]];
    state[15] = sbox[state[11]];
    state[11] = sbox[state[7]];
    state[7] = sbox[state[3]];
    state[3] = tmp;
}

/* ----------------------------------------------------------------------------
 * Function      : void inv_shift_rows_inv_sub_bytes(uint8_t *state)
 * ----------------------------------------------------------------------------
 * Description   : This function shifts rows to right and substitutes (rsbox)
 *                 the values in the state.
 * Inputs        : state - pointer to array holding the intermediate results.
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
static void inv_shift_rows_inv_sub_bytes(uint8_t *state)
{
    uint8_t tmp;

    /* row 1: rotate to right (1x) */
    tmp = state[13];
    state[13] = state[9];
    state[9] = state[5];
    state[5] = state[1];
    state[1] = tmp;

    /* row 2: rotate to right (2x) */
    tmp = state[10];
    state[10] = state[2];
    state[2] = tmp;

    tmp = state[14];
    state[14] = state[6];
    state[6] = tmp;

    /* row 3: rotate to right (3x) */
    tmp = state[3];

    state[3] = state[7];
    state[7] = state[11];
    state[11] = state[15];
    state[15] = tmp;

    /* Get the revert S-Box value */
    state[0] = rsbox[state[0]];
    state[1] = rsbox[state[1]];
    state[2] = rsbox[state[2]];
    state[3] = rsbox[state[3]];
    state[4] = rsbox[state[4]];
    state[5] = rsbox[state[5]];
    state[6] = rsbox[state[6]];
    state[7] = rsbox[state[7]];
    state[8] = rsbox[state[8]];
    state[9] = rsbox[state[9]];
    state[10] = rsbox[state[10]];
    state[11] = rsbox[state[11]];
    state[12] = rsbox[state[12]];
    state[13] = rsbox[state[13]];
    state[14] = rsbox[state[14]];
    state[15] = rsbox[state[15]];
}

/* ----------------------------------------------------------------------------
 * Function      : void mix_columns(uint8_t *state)
 * ----------------------------------------------------------------------------
 * Description   : This function mixes the columns of the state matrix
 * Inputs        : state - pointer to array holding the intermediate results.
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
static void mix_columns(uint8_t *state)
{
    uint8_t tmp[4];

    /* matrix representation of the Rijndael mix_columns */
    /* +-- --+   +--     --+ +-- --+
     * | d0  |   | 2 3 1 1 | | b0  |
     * | d1  |   | 1 2 3 1 | | b1  |
     * | d2  | = | 1 1 2 3 | | b2  |
     * | d3  |   | 3 1 1 3 | | b3  |
     * +-- --+   +--     --+ +-- --+
     *
     * Then :
     * d0 = 2*b0 + 3*b1 +   b2 +   b3
     * d1 =   b0 + 2*b1 + 3*b2 +   b3
     * d2 =   b0 +   b1 + 2*b2 + 3*b3
     * d3 = 3*b0 +   b1 +   b2 + 2*b3
     *
     * In the Galois field, addition is an exclusive OR and
     * multiplication is a complex operation which can be computed
     * or simply addressed by a lookup table.
     *
     * -> d0 = 2*b0 ^ 2*b1 ^ b1 ^ b2 ^ b3
     * -> d1 = 2*b1 ^ 2*b2 ^ b2 ^ b0 ^ b3
     * -> d2 = 2*b2 ^ 2*b3 ^ b3 ^ b0 ^ b1
     * -> d3 = 2*b3 ^ 2*b0 ^ b0 ^ b1 ^ b2
     */

    /* --------------------------------------------------------------------- */
    /* column 0 */
    /* --------------------------------------------------------------------- */
    tmp[0]   =
        gmult(state[0], 2) ^ gmult(state[1], 3) ^ state[2] ^ state[3];
    tmp[1]   =
        gmult(state[1], 2) ^ gmult(state[2], 3) ^ state[0] ^ state[3];
    tmp[2]   =
        gmult(state[2], 2) ^ gmult(state[3], 3) ^ state[0] ^ state[1];
    tmp[3]   =
        gmult(state[3], 2) ^ gmult(state[0], 3) ^ state[1] ^ state[2];
    state[0] = tmp[0];
    state[1] = tmp[1];
    state[2] = tmp[2];
    state[3] = tmp[3];

    /* --------------------------------------------------------------------- */
    /* column 1 */
    /* --------------------------------------------------------------------- */
    tmp[0]   =
        gmult(state[4], 2) ^ gmult(state[5], 3) ^ state[6] ^ state[7];
    tmp[1]   =
        gmult(state[5], 2) ^ gmult(state[6], 3) ^ state[4] ^ state[7];
    tmp[2]   =
        gmult(state[6], 2) ^ gmult(state[7], 3) ^ state[4] ^ state[5];
    tmp[3]   =
        gmult(state[7], 2) ^ gmult(state[4], 3) ^ state[5] ^ state[6];
    state[4] = tmp[0];
    state[5] = tmp[1];
    state[6] = tmp[2];
    state[7] = tmp[3];

    /* --------------------------------------------------------------------- */
    /* column 2 */
    /* --------------------------------------------------------------------- */
    tmp[0]    =
        gmult(state[8], 2) ^ gmult(state[9], 3) ^ state[10] ^ state[11];
    tmp[1]    =
        gmult(state[9], 2) ^ gmult(state[10], 3) ^ state[8] ^ state[11];
    tmp[2]    =
        gmult(state[10], 2) ^ gmult(state[11], 3) ^ state[8] ^ state[9];
    tmp[3]    =
        gmult(state[11], 2) ^ gmult(state[8], 3) ^ state[9] ^ state[10];
    state[8] = tmp[0];
    state[9] = tmp[1];
    state[10] = tmp[2];
    state[11] = tmp[3];

    /* --------------------------------------------------------------------- */
    /* column 3 */
    /* --------------------------------------------------------------------- */
    tmp[0]    =
        gmult(state[12], 2) ^ gmult(state[13], 3) ^ state[14] ^ state[15];
    tmp[1]    =
        gmult(state[13], 2) ^ gmult(state[14], 3) ^ state[12] ^ state[15];
    tmp[2]    =
        gmult(state[14], 2) ^ gmult(state[15], 3) ^ state[12] ^ state[13];
    tmp[3]    =
        gmult(state[15], 2) ^ gmult(state[12], 3) ^ state[13] ^ state[14];
    state[12] = tmp[0];
    state[13] = tmp[1];
    state[14] = tmp[2];
    state[15] = tmp[3];
}

/* ----------------------------------------------------------------------------
 * Function      : void inv_mix_columns(uint8_t *state)
 * ----------------------------------------------------------------------------
 * Description   : This function mixes the columns of the state matrix
 * Inputs        : state - pointer to array holding the intermediate results.
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void inv_mix_columns(uint8_t *state)
{
    uint8_t tmp[4];

    /* matrix representation of the Rijndael inv_mix_columns */
    /* +-- --+   +--         --+ +-- --+
     * | d0  |   | 14 11 13  9 | | b0  |
     * | d1  |   |  9 14 11 13 | | b1  |
     * | d2  | = | 13  9 14 11 | | b2  |
     * | d3  |   | 11 13  9 14 | | b3  |
     * +-- --+   +--         --+ +-- --+
     *
     * Then :
     * d0 = 14*b0 ^ 11*b1 ^ 13*b2 ^  9*b3
     * d1 =  9*b0 ^ 14*b1 ^ 11*b2 ^ 13*b3
     * d2 = 13*b0 ^  9*b1 ^ 14*b2 ^ 11*b3
     * d3 = 11*b0 ^ 13*b1 ^  9*b2 ^ 14*b3
     *
     */

    /* --------------------------------------------------------------------- */
    /* column 0 */
    /* --------------------------------------------------------------------- */
    tmp[0]   = gmult(state[0], 14) ^ gmult(state[1], 11) ^ gmult(state[2],
                                                                 13) ^ gmult(
        state[3], 9);
    tmp[1]   =
        gmult(state[0], 9) ^ gmult(state[1], 14) ^ gmult(state[2], 11) ^
        gmult(state[3], 13);
    tmp[2]   =
        gmult(state[0], 13) ^ gmult(state[1], 9) ^ gmult(state[2], 14) ^
        gmult(state[3], 11);
    tmp[3]   =
        gmult(state[0], 11) ^ gmult(state[1], 13) ^ gmult(state[2], 9) ^
        gmult(state[3], 14);
    state[0] = tmp[0];
    state[1] = tmp[1];
    state[2] = tmp[2];
    state[3] = tmp[3];

    /* --------------------------------------------------------------------- */
    /* column 1 */
    /* --------------------------------------------------------------------- */
    tmp[0]   = gmult(state[4], 14) ^ gmult(state[5], 11) ^ gmult(state[6],
                                                                 13) ^ gmult(
        state[7], 9);
    tmp[1]   =
        gmult(state[4], 9) ^ gmult(state[5], 14) ^ gmult(state[6], 11) ^
        gmult(state[7], 13);
    tmp[2]   =
        gmult(state[4], 13) ^ gmult(state[5], 9) ^ gmult(state[6], 14) ^
        gmult(state[7], 11);
    tmp[3]   =
        gmult(state[4], 11) ^ gmult(state[5], 13) ^ gmult(state[6], 9) ^
        gmult(state[7], 14);
    state[4] = tmp[0];
    state[5] = tmp[1];
    state[6] = tmp[2];
    state[7] = tmp[3];

    /* --------------------------------------------------------------------- */
    /* column 2 */
    /* --------------------------------------------------------------------- */
    tmp[0]    = gmult(state[8], 14) ^ gmult(state[9], 11) ^ gmult(state[10],
                                                                  13) ^ gmult(
        state[11], 9);
    tmp[1]    =
        gmult(state[8], 9) ^ gmult(state[9], 14) ^ gmult(state[10], 11) ^
        gmult(state[11], 13);
    tmp[2]    =
        gmult(state[8], 13) ^ gmult(state[9], 9) ^ gmult(state[10], 14) ^
        gmult(state[11], 11);
    tmp[3]    =
        gmult(state[8], 11) ^ gmult(state[9], 13) ^ gmult(state[10], 9) ^
        gmult(state[11], 14);
    state[8] = tmp[0];
    state[9] = tmp[1];
    state[10] = tmp[2];
    state[11] = tmp[3];

    /* --------------------------------------------------------------------- */
    /* column 3 */
    /* --------------------------------------------------------------------- */
    tmp[0]    = gmult(state[12], 14) ^ gmult(state[13], 11) ^ gmult(state[14],
                                                                    13) ^ gmult(
        state[15], 9);
    tmp[1]    =
        gmult(state[12], 9) ^ gmult(state[13], 14) ^ gmult(state[14], 11) ^
        gmult(state[15], 13);
    tmp[2]    =
        gmult(state[12], 13) ^ gmult(state[13], 9) ^ gmult(state[14], 14) ^
        gmult(state[15], 11);
    tmp[3]    =
        gmult(state[12], 11) ^ gmult(state[13], 13) ^ gmult(state[14], 9) ^
        gmult(state[15], 14);
    state[12] = tmp[0];
    state[13] = tmp[1];
    state[14] = tmp[2];
    state[15] = tmp[3];
}

/* ----------------------------------------------------------------------------
 * Function      : void aes128_sw_decipher(uint8_t *round_key, uint8_t *block)
 * ----------------------------------------------------------------------------
 * Description   : This function decrypts a ciphered text block.
 * Inputs        : round_key - pointer to the expanded keys
 *                 block     - pointer to ciphered block (128 bits)
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void aes128_sw_decipher(uint8_t *const round_key, uint8_t *block)
{
    uint8_t state[AES128_KEY_LENGTH];

    /* copy the plain text block */
    (void)memcpy(state, block, AES128_KEY_LENGTH);

    /* Add the last round key to the state before starting the rounds.*/
    add_round_key(&round_key[AES128_ROUNDS * AES128_KEY_LENGTH], state);

    for (uint8_t round = AES128_ROUNDS - 1; round >= 1; round--)
    {
        inv_shift_rows_inv_sub_bytes(state);
        add_round_key(&round_key[round * AES128_KEY_LENGTH], state);
        inv_mix_columns(state);
    }

    /* Last stage doesn't have the mix_columns */
    inv_shift_rows_inv_sub_bytes(state);
    add_round_key(round_key, state);

    (void)memcpy(block, state, AES128_KEY_LENGTH);
}

/* ----------------------------------------------------------------------------
 * Function      : void aes_sw_cipher(uint8_t *round_key, uint8_t *block)
 * ----------------------------------------------------------------------------
 * Description   : This function encrypts a plain text block.
 * Inputs        : round_key - pointer to the expanded keys
 *                 block     - pointer to plain block (128 bits)
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void aes128_sw_cipher(uint8_t *const round_key, uint8_t *block)
{
    uint8_t state[AES128_KEY_LENGTH];

    /* copy the plain text block */
    (void)memcpy(state, block, AES128_KEY_LENGTH);

    /* Add the First round key to the state before starting the rounds.*/
    add_round_key(round_key, state);

    for (uint8_t round = 1; round < AES128_ROUNDS; round++)
    {
        sub_bytes_shift_rows(state);
        mix_columns(state);
        add_round_key(&round_key[round * AES128_KEY_LENGTH], state);
    }

    /* Last stage doesn't have the mix_columns */
    sub_bytes_shift_rows(state);
    add_round_key(&round_key[AES128_ROUNDS * AES128_KEY_LENGTH], state);

    (void)memcpy(block, state, AES128_KEY_LENGTH);
}

/* ----------------------------------------------------------------------------
 * Function      : void aes128_hw_cipher(uint8_t *const key, const uint32_t em,
 *                                       uint8_t *block)
 * ----------------------------------------------------------------------------
 * Description   : This function encrypts a plain text block.
 * Inputs        : em    - address in the Exchange Memory to store plain text.
 *                 key   - pointer to the encryption key
 *                 block - pointer to plain block
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void aes128_hw_cipher(uint8_t *const key, const uint32_t em, uint8_t *block)
{
    uint32_t *p_tmp;
    uint32_t *p_em;

    /* ---------------------------------------------------------------------- */
    /* Configure the AES pointer.
     * Address to exchange memory can be passed regarding system mapping
     * or base band mapping. */
    if (em >= BB_DRAM0_BASE)
    {
        BB->AESPTR = em - BB_DRAM0_BASE;
        p_em = (uint32_t *)(em);
    }
    else
    {
        BB->AESPTR = em;
        p_em = (uint32_t *)(BB_DRAM0_BASE + em);
    }

    /* ---------------------------------------------------------------------- */
    /* Convert entries from little endian to big endian */
    /* ---------------------------------------------------------------------- */

    /* Copy 128-bit encryption key */
    p_tmp = (uint32_t *)(&key[0]);
    BB->AESKEY127_96 = __REV(p_tmp[0]);
    BB->AESKEY95_64  = __REV(p_tmp[1]);
    BB->AESKEY63_32  = __REV(p_tmp[2]);
    BB->AESKEY31_0   = __REV(p_tmp[3]);

    /* copy data block to cipher into the exchange memory space */
    p_tmp   = (uint32_t *)(&block[0]);
    p_em[3] = __REV(p_tmp[0]);
    p_em[2] = __REV(p_tmp[1]);
    p_em[1] = __REV(p_tmp[2]);
    p_em[0] = __REV(p_tmp[3]);

    /* ---------------------------------------------------------------------- */
    /* Enable baseband */
    BBIF_CTRL->CLK_ENABLE_ALIAS = BB_CLK_ENABLE_BITBAND;

    /* Start AES encryption */
    BB->AESCNTL = AES_MODE_0 | AES_START_1;

    /* Wait for the AES transfer to complete */
    while (BB_AESCNTL->AES_START_ALIAS != AES_START_0_BITBAND);

    /* ---------------------------------------------------------------------- */
    /* Convert entries from big endian to little endian */
    /* ---------------------------------------------------------------------- */
    p_tmp[0] = __REV(p_em[7]);
    p_tmp[1] = __REV(p_em[6]);
    p_tmp[2] = __REV(p_em[5]);
    p_tmp[3] = __REV(p_em[4]);
}
