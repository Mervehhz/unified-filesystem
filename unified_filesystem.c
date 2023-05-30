#define FUSE_USE_VERSION 26
#define _GNU_SOURCE

#include <fuse.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/vfs.h>
#include <fcntl.h>
#include <unistd.h>
#include "aes.h"
#include "utils.h"

struct AES_ctx ctx;

static const char *filepath = "/file";
static const char *filename = "file";
static const char *filecontent = "I'm the content of the only file available there\n";

struct drives all_drives, input_drives;
struct directory root;

struct directory* get_directory(char* path) {
    if(strcmp(path, "/") == 0) {
        return &root;
    }
    // see if path exists under root tree
    struct directory* current = &root;
    char* token = strtok(path + 1, "/");
    while(token != NULL) {
        if(current->children == NULL) {
            return NULL;
        }
        // check if token exists in current directory
        BYTE found = 0;
        for(size_t i = 0; i < current->num_children; i++) {
            printf("CHILD: %s\n", current->children[i].name);
            if(strcmp(current->children[i].name, token) == 0) {
                current = &current->children[i];
                found = 1;
                break;
            }
        }
        if(!found) {
            return NULL;
        }
        token = strtok(NULL, "/");
    }

    return current;
}

void split_path_and_file_name(const char* path, char** file_name, char** rest_of_path) {
    const char* lastSlash = strrchr(path, '/');
    if (lastSlash != NULL) {
        // found a slash, split the path and filename
        *rest_of_path = malloc(lastSlash - path + 2);  // allocate memory for directory
        strncpy(*rest_of_path, path, lastSlash - path + 1);
        (*rest_of_path)[lastSlash - path + 1] = '\0';
        *file_name = strdup(lastSlash + 1);  // duplicate the filename string
    } else {
        // no slash found, assume the whole string is the filename
        *rest_of_path = strdup("");
        *file_name = strdup(path);
    }
}


void pcks(BYTE* buf, size_t len){
    BYTE padding = 16 - len;
    for(size_t i=len;i<16;i++){
        buf[i] = padding;
    }
}


BYTE* encrypt(BYTE* part, size_t size) {
    // TODO: encrypt parts 
    BYTE *aes_part = (BYTE*) malloc(sizeof(BYTE)*16);
    size_t aes_part_count = size%16 == 0 ? size/16 : size/16 +1; 
    uint8_t* encrypted_part = (uint8_t*) malloc(sizeof(uint8_t)*(aes_part_count*16));
    size_t offset = 0, curr_size = size;
    uint8_t* encrypted_part_head = encrypted_part;
    for(size_t k=0;k<aes_part_count;k++){ 
        offset = curr_size <= 16 ? curr_size : 16; 
        memcpy(aes_part, part, offset); 
        pcks(aes_part, offset);
        AES_ECB_encrypt(&ctx, aes_part);
        memcpy(encrypted_part, aes_part, 16);
        encrypted_part += 16;
        part += offset;
        curr_size -= offset; 
    }

    encrypted_part_head = realloc(encrypted_part_head, (aes_part_count + 1) * 16);
    memset(encrypted_part, 0x10, 16);
    AES_ECB_encrypt(&ctx, encrypted_part);
    
    free(aes_part); 
    return encrypted_part_head;
}

BYTE* decrypt(BYTE* part, size_t size) {
    // TODO: decrypt parts    
    BYTE* decrypted_part = (BYTE*) malloc(sizeof(BYTE)*size);
    BYTE *aes_part = (BYTE*) malloc(sizeof(BYTE)*16);
    size_t aes_part_count = size%16 == 0 ? size/16 : size/16 +1; 
    size_t offset = 0, curr_size = size;

    
    BYTE* decrypted_part_head = decrypted_part;

    for(size_t k=0;k<aes_part_count;k++){ 
        offset = curr_size <= 16 ? curr_size : 16; 
        memcpy(aes_part, part, 16); 
        AES_ECB_decrypt(&ctx, aes_part);
        memcpy(decrypted_part, aes_part, offset);
        decrypted_part += offset;
        part += offset;
        curr_size -= offset; 
    }

    free(aes_part);
    return decrypted_part_head;
}


// Takes data and splits into 8KB parts and also sets part_count to return part count
BYTE** split_file(const BYTE* data, size_t size, size_t* part_count) {
    // split data into 8KB parts
    size_t part_size =size >=  8 * 1024 ? 8*1024 : size;
    *(part_count) = size / part_size + ((size % part_size != 0) ? 1 : 0);
    BYTE** parts = (BYTE**) malloc(sizeof(BYTE*)*(*part_count));
    for(size_t i = 0;i < *part_count;i++) {
        parts[i] = malloc(sizeof(BYTE) * part_size);
    }
    size_t wrote_bytes = 0;
    for(size_t i = 0; i < *part_count; i++) {
        size_t curr_part_size = ((size - wrote_bytes) >=  8 *1024) ? 8 * 1024 : (size - wrote_bytes);
        memcpy(parts[i], data + wrote_bytes, curr_part_size);
        wrote_bytes += curr_part_size;
    }
    
    return parts;
}


void get_drive_info(char* path, size_t *total, size_t *free) {
    struct statfs st;
    statfs(path, &st);
    *total = st.f_blocks * st.f_bsize;
    *free = st.f_bfree * st.f_bsize;
}


BYTE* get_metadata(char* file_name, size_t part_count, size_t part_index) {
    // add metadata to parts
    // metadata format: <file_name length in 8 bytes (64 bit)> <file name> <part_index in 8 bytes (64 bit)> <part_count in 8 bytes (64 bit)>
    size_t metadata_size = 8 + strlen(file_name) + 8 + 8;
    BYTE* metadata = (BYTE*) calloc(metadata_size, sizeof(BYTE));

    // add file name length
    size_t file_name_length = strlen(file_name);
    memcpy(metadata, &file_name_length, 8);

    // add file name
    memcpy(metadata + 8, file_name, strlen(file_name));

    // add part index
    memcpy(metadata + 8 + strlen(file_name), &part_index, 8);

    // add part count
    memcpy(metadata + 8 + strlen(file_name) + 8, &part_count, 8);

    return metadata;
}



// write each part to drives with checking free space in every iteration 
void write_to_drives(const BYTE* data, size_t size, struct file* file, char* path) {
    
    size_t part_count = 0;
    BYTE** parts = split_file(data, size, &part_count);
    size_t wrote_bytes = 0;
    int part_file;
    char* part_path;

    for(size_t i = 0; i < part_count;i++) {
        size_t total, free_space;

        size_t max_free = 0, max_free_index = 0;
        for(size_t j = 0; j < input_drives.num; j++) {
                get_drive_info(input_drives.paths[j], &total, &free_space);
                if(free_space > max_free) {
                    max_free = free_space;
                    max_free_index = j;
                }
            }

        printf("Writing part  to drive %s\n", input_drives.paths[max_free_index]);

        // write part to drive
        part_path = (char*) malloc(sizeof(char) * (strlen(path) + strlen(input_drives.paths[max_free_index]) + strlen(file->file_name) + 2));

        unsigned long part_timestamp = time(NULL);

        sprintf(part_path, "%s%s%lu_%ld", input_drives.paths[max_free_index], path, part_timestamp, file->part_count + 1);

        printf("Part path: %s\n", part_path);

        part_file = open(part_path, O_CREAT | O_WRONLY, 0777);
        if(part_file == -1) {
            printf("Error opening file %s", part_path);
            exit(1);
        }

        size_t part_size = ((size - wrote_bytes) >= 8 * 1024) ? 8 * 1024 : (size - wrote_bytes);
        BYTE* encrypted_part = encrypt(parts[i], part_size);
        part_size += 16 - (part_size % 16);
        int written_bytes = write(part_file, encrypted_part, part_size); 

        if(file->parts == NULL) {
            file->parts =  (struct file_part*) malloc(sizeof(struct file_part) * (file->part_count + 1));
        } else {
            file->parts = (struct file_part*) realloc(file->parts, sizeof(struct file_part) * (file->part_count + 1));
        }

        file->parts[file->part_count].timestamp = part_timestamp;
        file->parts[file->part_count].part_path = (char*) malloc(sizeof(char) * (strlen(part_path) + 1));
        strcpy(file->parts[file->part_count].part_path, part_path);
        file->parts[file->part_count].size = size;
        file->parts[file->part_count].part_no = file->part_count + 1;

        file->part_count++;
    }
    close(part_file);
    free(part_path);
    file->size += size;
}

static int release_callback(const char *path, struct fuse_file_info *fi) {
    return 0;
}

static int truncate_callback(const char *path, off_t size) {
    return 0;
}

static int unlink_callback(const char *path) {
    return 0;
}

static int utimens_callback(const char *path, const struct timespec ts[2]) {
    
    char* file_name, *rest_of_path;
    split_path_and_file_name(path, &file_name, &rest_of_path);

    printf("utimens_callback: %s %s\n", file_name, rest_of_path);

    struct directory* dir = get_directory(rest_of_path);

    for(size_t i = 0; i < dir->num_files; i++) {
        if(strcmp(dir->files[i].file_name, file_name) == 0) {
            dir->files[i].atime = ts[0];
            dir->files[i].mtime = ts[1];
            break;
        }
    }

    return 0;
}

static int chown_callback(const char *path, uid_t uid, gid_t gid) {
    return 0;
}

static int create_callback(const char *path, mode_t mode, struct fuse_file_info *fi) {

    printf("CREATE CALLBACK: %s\n", path);
    char *file_name, *rest_of_path;
    split_path_and_file_name(path, &file_name, &rest_of_path);

    printf("create_callback: %s %s\n", file_name, rest_of_path);

    struct directory* dir = get_directory(rest_of_path);

    printf("dir.name: %s\n", dir->name);
    if(dir->num_files) {
        dir->num_files++;
        dir->files = (struct file*) realloc(dir->files, sizeof(struct file) * dir->num_files);
    } else {
        dir->num_files = 1;
        dir->files = (struct file*) malloc(sizeof(struct file));
    }
    dir->files[dir->num_files - 1].file_name = (char*) malloc(sizeof(char) * strlen(file_name));
    strcpy(dir->files[dir->num_files - 1].file_name, file_name);
    dir->files[dir->num_files - 1].size = 0;
    dir->files[dir->num_files - 1].mode = 0777;
    dir->files[dir->num_files - 1].part_count = 0;
    dir->files[dir->num_files - 1].parts = NULL;

    return 0;
}

static int rename_callback(const char *path, const char *new_path, unsigned int flags) {

    char *file_name, *rest_of_path, *new_file_name, *new_rest_of_path;
    split_path_and_file_name(path, &file_name, &rest_of_path);
    split_path_and_file_name(new_path, &new_file_name, &new_rest_of_path);

    printf("rename_callback: %s %s\n", file_name, rest_of_path);
    printf("rename_callback: %s %s\n", new_file_name, new_rest_of_path);

    struct directory* dir = get_directory(rest_of_path);
    // delete old file if exists
    for(size_t i = 0; i < dir->num_files; i++) {
        if(strcmp(dir->files[i].file_name, new_file_name) == 0) {
            // delete this file and move all other files to the left
            free(dir->files[i].file_name);
            for(size_t j = i; j < dir->num_files - 1; j++) {
                dir->files[j] = dir->files[j + 1];
            }
            dir->num_files--;
        }
    }


    for(size_t i = 0; i < dir->num_files; i++) {
        if(strcmp(dir->files[i].file_name, file_name) == 0) {
            free(dir->files[i].file_name);
            dir->files[i].file_name = (char*) calloc(strlen(new_file_name) + 1, sizeof(char));
            strcpy(dir->files[i].file_name, new_file_name);
            break;
        }
    }

    return 0;
}


static int mkdir_callback(const char *path, mode_t mode) {
    char *file_name, *rest_of_path;
    split_path_and_file_name(path, &file_name, &rest_of_path);

    printf("mkdir_callback: %s %s\n", file_name, rest_of_path);


    struct directory* dir = get_directory(rest_of_path);

    if(!dir) {
        return -1;
    }

    printf("dir.name: %s\n", dir->name);
    if(dir->num_children) {
        dir->num_children++;
        dir->children = (struct directory*) realloc(dir->children, sizeof(struct directory) * dir->num_children);
    } else {
        dir->num_children = 1;
        dir->children = (struct directory*) malloc(sizeof(struct directory));
    }
    dir->children[dir->num_children - 1].name = (char*) malloc(sizeof(char) * strlen(file_name));
    strcpy(dir->children[dir->num_children - 1].name, file_name);
    dir->children[dir->num_children - 1].num_children = 0;
    dir->children[dir->num_children - 1].children = NULL;
    dir->children[dir->num_children - 1].num_files = 0;
    dir->children[dir->num_children - 1].files = NULL;
    dir->children[dir->num_children - 1].mode = 0777;

    return 0;
}

static int releasedir_callback(const char *path, struct fuse_file_info *fi) {
    return 0;
}


static int setattr_callback(const char *path, struct stat *stbuf, int to_set) {
    return 0;
}

// /merve/ozan/xyz -> /merve/ozan xyz  /merve/ozan/xyz/
static int getattr_callback(const char *path, struct stat *stbuf) {
    memset(stbuf, 0, sizeof(struct stat));

    if(strcmp(path, "/") == 0) {
        stbuf->st_mode = S_IFDIR | 0777;
        stbuf->st_nlink = 2;
        return 0;
    }

    // get file name from path by splitting with '/' and taking last token and get rest of the path
    char* file_name;
    char* rest_of_path;

    split_path_and_file_name(path, &file_name, &rest_of_path);

    // edge case for root directory
    if(strcmp(rest_of_path, "") == 0) {
        strcpy(rest_of_path, "/");
    }

    printf("getattr_callback: %s %s\n", file_name, rest_of_path);

    if(strlen(file_name) == 0) {
        struct directory* dir = get_directory(rest_of_path);

        if(dir != NULL) {
            stbuf->st_mode = S_IFDIR | dir->mode;
            stbuf->st_nlink = 2;
            return 0;
        } else {
            return -ENOENT;
        }
    } else {
        // first check if full path is a directory
        struct directory* dir = get_directory(path);
        if(dir != NULL) {
            stbuf->st_mode = S_IFDIR | dir->mode;
            stbuf->st_nlink = 2;
            return 0;
        }

        // check if file exists
        dir = get_directory(rest_of_path);
        if(dir != NULL) {
            for(size_t i = 0; i < dir->num_files; i++) {
                if(strcmp(dir->files[i].file_name, file_name) == 0) {
                    stbuf->st_mode = S_IFREG | dir->files[i].mode;
                    stbuf->st_nlink = 1;
                    stbuf->st_size = dir->files[i].size;
                    return 0;
                }
            }
        } else {
            printf("NOT FOUND\n");
            return -ENOENT;
        }
    }

    return -ENOENT;
}

static int readdir_callback(const char *path, void *buf, fuse_fill_dir_t filler,
    off_t offset, struct fuse_file_info *fi) {

    struct directory* current = get_directory((char*) path);
    if(current == NULL) {
        return -ENOENT;
    }
    
    (void) offset;
    (void) fi;

    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);

    // traverse to the directory from root
    for(size_t i = 0;i < current->num_files;i++) {
        filler(buf, current->files[i].file_name, NULL, 0);
    }

    for(size_t i = 0;i < current->num_children;i++) {
        filler(buf, current->children[i].name, NULL, 0);
    }

    return 0;
}

static int open_callback(const char *path, struct fuse_file_info *fi) {
    return 0;
}

static int fsync_callback(const char *path, int isdatasync, struct fuse_file_info *fi) {
    return 0;
}

static int read_callback(const char *path, char *buf, size_t size, off_t offset,
    struct fuse_file_info *fi) {

    char* file_name;
    char* remaining_path;

    split_path_and_file_name(path, &file_name, &remaining_path);

    struct directory* dir = get_directory(remaining_path);

    struct file* file;
    for(size_t i = 0; i < dir->num_files; i++) {
        if(strcmp(dir->files[i].file_name, file_name) == 0) {
            file = &dir->files[i];
            break;
        }
    }

    BYTE* data = (BYTE*) malloc(size);  
    size_t current_offset = 0;
    size_t diff = 0;
    for(size_t i = 0; i < file->part_count; i++) {
        size_t part_size = file->parts[i].size;
        if(current_offset < offset) {
            if(current_offset + part_size > offset) {
                diff = offset - current_offset;
                part_size -= diff;
                current_offset += diff; 
            } else { 
                current_offset += part_size;
                continue;
            }
        }
        part_size = (part_size <= size ) ? part_size : size;

        int part_file = open(file->parts[i].part_path, O_RDONLY, 0777);
        int read_size = part_size + 16 - (part_size % 16);
        BYTE* part_data = (BYTE*) malloc(read_size);
        int res = pread(part_file, part_data, read_size, diff);
        BYTE* decrypted_part_data = decrypt(part_data, read_size); 
        
        if(res == -1) {
            perror("pread");
            return -1;
        }
        close(part_file);

        memcpy(data + current_offset - offset, decrypted_part_data, part_size);
        current_offset += part_size;
        free(decrypted_part_data);


        if(current_offset >= offset + size) {
            break;
        }
    }
    memcpy(buf, data, size);

    free(data);

    return size;
}

static int write_callback(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    
    size_t part_count;
    // get file name from path (last part of path)
    char* file_name;
    char* remaining_path;

    split_path_and_file_name(path, &file_name, &remaining_path);

    struct directory* dir = get_directory(remaining_path);
    struct file* file;
    for(size_t i = 0; i < dir->num_files; i++) {
        if(strcmp(dir->files[i].file_name, file_name) == 0) {
            file = &dir->files[i];
            break;
        }
    }

    if(file == NULL) {
        printf("file not found\n");
        return -ENOENT;
    }
    
    write_to_drives(buf, size, file, remaining_path);
    
    return size;
}


static struct fuse_operations fuse_example_operations = {
    .getattr = getattr_callback,
    .open = open_callback,
    .read = read_callback,
    .readdir = readdir_callback,
    .write = write_callback,
    .create = create_callback,
    .unlink = unlink_callback,
    .truncate = truncate_callback,
    .release = release_callback,
    .mkdir = mkdir_callback,
    .releasedir = releasedir_callback,
    .utimens = utimens_callback,
    .chown = chown_callback,
    .fsync = fsync_callback,
    .rename = rename_callback,
};



int main(int argc, char *argv[])
{   

    FILE* fileopen;
    char line[130];

    fileopen = popen("lsblk | grep /media | awk -F'part /' '{print \"/\"$NF}'", "r");

    all_drives.num = 0;

    for(int i=0;fgets(line, sizeof line, fileopen) != NULL; i++){
        if(i == 0){
            all_drives.paths = (char**) malloc(sizeof(char*));
        }
        else{
            all_drives.paths = (char**) realloc(all_drives.paths, sizeof(char*)*(i+1));
        }

        line[strcspn(line, "\n")] = 0;
        all_drives.paths[i] = (char*) malloc(sizeof(char)*strlen(line));
        strcpy(all_drives.paths[i], line);
        all_drives.num++;
    }

    int argc_ = 3;
    char* argv_[3];

    argv_[0] = (char*) malloc(sizeof(char)*strlen(argv[0]));
    strcpy(argv_[0], argv[0]);

    input_drives.num = 0;

    for(int i=0, k=0;i<argc;i++){
        if(strcmp(argv[i], "--virtual_drive_path") == 0){
            argv_[1] = (char*) malloc(sizeof(char)*strlen(argv[i+1]));
            strcpy(argv_[1], argv[i+1]);
        }

        if(strcmp(argv[i], "--hard_drive") == 0){
            if(k == 0){
                input_drives.paths = (char**) malloc(sizeof(char*));
            }
            else{
                input_drives.paths = (char**) realloc(input_drives.paths, sizeof(char*)*(k+1));
            }
            
            input_drives.paths[k] = (char*) malloc(sizeof(char)*strlen(argv[i+1]));
            strcpy(input_drives.paths[k], argv[i+1]);
            input_drives.num++;
            k++;
        }
    }

    // set fuse in debug mode
    argv_[2] = (char*) malloc(sizeof(char)*strlen("-d"));
    strcpy(argv_[2], "-d");

    int temp = 0;

    for(int i=0;i<input_drives.num;i++){
        for(int k=0;k<all_drives.num;k++){
            if(strcmp(input_drives.paths[i], all_drives.paths[k]) == 0){
                temp++;
                break;
            }
        }
    }

    if(temp != input_drives.num){
        fprintf(stderr, "bad file name...");
    }

    root.num_children = 0;
    root.children = NULL;
    root.name = "/";
    root.num_files = 0;

    AES_init_ctx(&ctx, key);

    return fuse_main(argc_, argv_, &fuse_example_operations, NULL);
}

