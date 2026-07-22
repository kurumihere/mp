#ifndef MP_FOLDER_TREE_H
#define MP_FOLDER_TREE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "playlist.h"

typedef struct {
    char *path;
    char *name;
    bool expanded;
    bool root;
    size_t parent_index;
    size_t depth;
    size_t first_track;
} Folder_Tree_Folder;

typedef struct {
    bool folder;
    size_t folder_index;
    size_t track_index;
} Folder_Tree_Row;

typedef struct {
    Folder_Tree_Folder *folders;
    size_t folder_count;
    Folder_Tree_Row *rows;
    size_t row_count;
    uint64_t playlist_signature;
} Folder_Tree;

void folder_tree_init(Folder_Tree *tree);
void folder_tree_uninit(Folder_Tree *tree);
bool folder_tree_sync(Folder_Tree *tree, const Playlist *playlist);

size_t folder_tree_folder_count(const Folder_Tree *tree);
size_t folder_tree_visible_count(const Folder_Tree *tree);
bool folder_tree_visible_row(const Folder_Tree *tree, size_t visible_index,
                             Folder_Tree_Row *row);
const char *folder_tree_folder_name(const Folder_Tree *tree,
                                    size_t folder_index);
bool folder_tree_folder_expanded(const Folder_Tree *tree, size_t folder_index);
bool folder_tree_folder_is_root(const Folder_Tree *tree, size_t folder_index);
size_t folder_tree_folder_depth(const Folder_Tree *tree, size_t folder_index);
bool folder_tree_folder_contains(const Folder_Tree *tree, size_t folder_index,
                                 size_t candidate_index);
void folder_tree_toggle(Folder_Tree *tree, size_t folder_index);

#endif
