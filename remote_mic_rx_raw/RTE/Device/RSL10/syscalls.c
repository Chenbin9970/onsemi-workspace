/**
 * @file syscalls.c
 * @brief Stubs for Newlib system calls.
 *
 * @copyright @parblock
 * Copyright (c) 2023 Semiconductor Components Industries, LLC (d/b/a
 * onsemi), All Rights Reserved
 *
 * This code is the property of onsemi and may not be redistributed
 * in any form without prior written permission from onsemi.
 * The terms of use and warranty for this code are covered by contractual
 * agreements between onsemi and the licensee.
 *
 * This is Reusable Code.
 * @endparblock
 */

#ifdef __GNUC__
#include <sys/stat.h>
#include <sys/unistd.h>

/* ----------------------------------------------------------------------------
 * If building with a C++ compiler, make all of the definitions in this header
 * have a C binding.
 * ------------------------------------------------------------------------- */
#ifdef __cplusplus
extern "C"
{
#endif    /* ifdef __cplusplus */

/* Function Prototypes */
int _link(char *old, char *new);
int _close(int file);
int _fstat(int file, struct stat *st);
int _isatty(int file);
int _lseek(int file, int ptr, int dir);
void _exit(int status);
int _kill(int pid, int sig);
int _getpid(void);
int _write(int file, char* ptr, int len);
int _read(int file, char* ptr, int len);

/**
 * @brief       Generic function to perform a semihosting call
 * @param[in]   reason specifies the particular semihosting operation to perform
 * @param[in]   arg  pointer pointing to an argument block that contains additional
 *                   information needed for that operation
 * @return      returns the value contained in the r0 register
 */
static inline int do_Semihosting (int reason, void * arg)
{
    int value;
    __asm volatile ("mov r0, %1; mov r1, %2; bkpt %a3; mov %0, r0"
           : "=r" (value) /* Outputs */
           : "r" (reason), "r" (arg), "i" (0xAB) /* Inputs */
           : "r0", "r1", "r2", "r3", "ip", "lr", "memory", "cc"
            /* Clobbers r0, r1, r2, r3, ip, lr, memory and cc if in supervisor mode */);
    /* Just to be on the safe side with clobbered registers,
     * we are doing the same thing as newlib, which has the following comment:
     *
     * Accordingly to page 13-77 of ARM DUI 0040D other registers
     * can also be clobbered.  Some memory positions may also be
     * changed by a system call, so they should not be kept in registers. */

    return value;
}

/**
 * @brief       Establish a new name for an existing file.
 * @param[in]   old Old name of the file
 * @param[in]   new New name of the file
 * @return      returns -1
 */
int __attribute__((weak)) _link(char *old, char *new)
{
    return -1;
}

/**
 * @brief       Close a file.
 * @param[in]   file File descriptor
 * @return      returns -1
 */
int __attribute__((weak)) _close(int file)
{
    return -1;
}

/**
 * @brief       Status of an open file.
 * @param[in]   file File descriptor
 * @param[in]   *st Pointer to the stat struct
 * @return      returns 0
 */
int __attribute__((weak)) _fstat(int file, struct stat *st)
{
    st->st_mode = S_IFCHR;

    return 0;
}

/**
 * @brief       Query whether output stream is a terminal.
 * @param[in]   file File descriptor
 * @return      returns 1
 */
int __attribute__((weak)) _isatty(int file)
{
    return 1;
}

/**
 * @brief       Set position in a file.
 * @param[in]   file File descriptor
 * @param[in]   ptr Offset Value
 * @param[in]   dir Position used as a reference for the offset
 * @return      returns 0
 */
int __attribute__((weak)) _lseek(int file, int ptr, int dir)
{
    return 0;
}

/**
 * @brief       Exit a program without cleaning up files.
 * @param[in]   status Exit Status
 */
void __attribute__((weak)) _exit(int status)
{
    while (1) {}
}

/**
 * @brief       Send a kill signal.
 * @param[in]   pid Process id
 * @param[in]   sig Signal to send
 * @return      returns -1
 */
int __attribute__((weak)) _kill(int pid, int sig)
{
    return -1;
}

/**
 * @brief       Get the process id.
 * @return      returns -1
 */
int __attribute__((weak)) _getpid(void)
{
    return -1;
}

/**
 * @brief       Read from a file.
 * @param[in]   file File descriptor
 * @param[in]   ptr Pointer to a buffer where read data will be stored
 * @param[in]   len Number of bytes to read from file
 * @return      return the actual number of bytes read
 *              if no error occurs, otherwise return -1
 */
int __attribute__((weak)) _read (int file, char *ptr, int len)
{
    if (file != STDIN_FILENO)
    {
        return -1;
    }

    int block[3];
    block[0] = file;
    block[1] = (int)ptr;
    block[2] = len;

    int ret = do_Semihosting(0x06, block);

    if (ret < 0)
    {
        return -1;
    }

    return ret;
}

/**
 * @brief       Write to a file.
 * @param[in]   file File descriptor
 * @param[in]   ptr Pointer to a buffer that contains data to write
 * @param[in]   len Number of bytes that should be written
 * @return      return len if no error occurs, otherwise return -1
 */
int __attribute__((weak)) _write(int file, char *ptr, int len)
{
    if (file != STDOUT_FILENO && file != STDERR_FILENO)
    {
        return -1;
    }

    int block[3];
    block[0] = file;
    block[1] = (int)ptr;
    block[2] = len;

    int ret = do_Semihosting(0x05, block);

    if (ret < 0)
    {
        return -1;
    }

    return len;
}

/* ----------------------------------------------------------------------------
 * Close the 'extern "C"' block
 * ------------------------------------------------------------------------- */
#ifdef __cplusplus
}
#endif    /* __cplusplus */
#endif    /* ifdef __GNUC__ */
