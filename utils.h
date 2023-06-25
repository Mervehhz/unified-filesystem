#pragma once

#include <stdint.h>
#include <stdio.h>
#include <time.h>

/* 256 bit key */
uint8_t key[] = { 'm', 'e', 'r', 'v',
                                'e', 'm', 'e', 'r', 
                                'v', 'e', 'm', 'e', 
                                'r', 'v', 'e', 'm' };

struct Header
{
    char* file_name;
    int part_no;
};

// used to store hard drives paths and number of drives
struct drives {
    char** paths;
    size_t num;
};

// used to store file parts
struct file_part {
    unsigned long timestamp;
    size_t part_no;
    char* part_path;
    size_t size;
};

// used to store file information
struct file {  
    char* file_name;
    char* file_path;
    size_t size;
    size_t part_count;
    int mode;
    // access time
    struct timespec atime;
    // modify time
    struct timespec mtime;
    struct file_part* parts;
};

// used to store directory information
struct directory {
    struct directory* parent;
    struct directory* children;
    struct file* files;
    size_t num_children;
    size_t num_files;
    char* name;
    int mode;
};

typedef char BYTE;