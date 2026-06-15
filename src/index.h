#ifndef INDEX_H
#define INDEX_H

void index_add(
    const char* original_path,
    const char* hash, 
    long size_bytes, 
    long last_modified, 
    const char* file_type
);

#endif