#include "folder_tree.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static char *copy_range(const char *text, size_t length)
{
    char *copy = malloc(length + 1);

    if (copy == NULL) return NULL;
    memcpy(copy, text, length);
    copy[length] = '\0';
    return copy;
}

static const char *last_separator(const char *path)
{
    const char *slash = strrchr(path, '/');
    const char *backslash = strrchr(path, '\\');

    if (slash == NULL) return backslash;
    if (backslash == NULL) return slash;
    return slash > backslash ? slash : backslash;
}

static char *parent_path(const char *path)
{
    const char *separator = last_separator(path);

    if (separator == NULL) return copy_range("", 0);

    size_t length = (size_t)(separator - path);

    while (length > 1 &&
           (path[length - 1] == '/' || path[length - 1] == '\\')) {
        --length;
    }

    return copy_range(path, length);
}

static char *folder_name(const char *path)
{
    if (path[0] == '\0') return copy_range("Files", sizeof("Files") - 1);

    const char *separator = last_separator(path);
    const char *name = separator == NULL ? path : separator + 1;

    if (name[0] == '\0') name = path;
    return copy_range(name, strlen(name));
}

static bool path_contains(const char *root, const char *path)
{
    size_t length = strlen(root);

    if (strncmp(root, path, length) != 0) return false;
    if (path[length] == '\0' || length == 0) return true;
    if (length == 1 && (root[0] == '/' || root[0] == '\\')) return true;
    return path[length] == '/' || path[length] == '\\';
}

static uint64_t playlist_signature(const Playlist *playlist)
{
    uint64_t hash = UINT64_C(1469598103934665603);
    size_t count = playlist_get_count(playlist);

    for (size_t i = 0; i < count; ++i) {
        const unsigned char *path =
            (const unsigned char *)playlist_get(playlist, i);

        while (path != NULL && *path != '\0') {
            hash ^= *path++;
            hash *= UINT64_C(1099511628211);
        }

        hash ^= UINT64_C(255);
        hash *= UINT64_C(1099511628211);
    }

    hash ^= count;
    return hash;
}

static bool old_expanded(const Folder_Tree *tree, const char *path)
{
    for (size_t i = 0; i < tree->folder_count; ++i) {
        if (strcmp(tree->folders[i].path, path) == 0) {
            return tree->folders[i].expanded;
        }
    }

    return false;
}

static size_t find_folder(const Folder_Tree *tree, const char *path)
{
    for (size_t i = 0; i < tree->folder_count; ++i) {
        if (strcmp(tree->folders[i].path, path) == 0) return i;
    }

    return SIZE_MAX;
}

static bool add_folder(Folder_Tree *tree, const Folder_Tree *old_tree,
                       const char *path, size_t parent_index,
                       size_t first_track, bool root, size_t *folder_index)
{
    size_t existing = find_folder(tree, path);

    if (existing != SIZE_MAX) {
        if (first_track < tree->folders[existing].first_track) {
            tree->folders[existing].first_track = first_track;
        }
        *folder_index = existing;
        return true;
    }

    Folder_Tree_Folder *folders = realloc(
        tree->folders, (tree->folder_count + 1) * sizeof(*tree->folders));

    if (folders == NULL) return false;
    tree->folders = folders;

    char *path_copy = copy_range(path, strlen(path));
    char *name = folder_name(path);

    if (path_copy == NULL || name == NULL) {
        free(path_copy);
        free(name);
        return false;
    }

    size_t index = tree->folder_count++;
    tree->folders[index] = (Folder_Tree_Folder){
        .path = path_copy,
        .name = name,
        .expanded = root || old_expanded(old_tree, path),
        .root = root,
        .parent_index = parent_index,
        .depth = parent_index == SIZE_MAX
                     ? 0
                     : tree->folders[parent_index].depth + 1,
        .first_track = first_track,
    };
    *folder_index = index;
    return true;
}

static bool ensure_folder(Folder_Tree *tree, const Folder_Tree *old_tree,
                          const char *root_path, const char *path,
                          size_t first_track, size_t *folder_index)
{
    size_t existing = find_folder(tree, path);

    if (existing != SIZE_MAX) {
        if (first_track < tree->folders[existing].first_track) {
            tree->folders[existing].first_track = first_track;
        }
        *folder_index = existing;
        return true;
    }

    if (strcmp(path, root_path) == 0) {
        return add_folder(tree, old_tree, path, SIZE_MAX, first_track, true,
                          folder_index);
    }

    char *parent = parent_path(path);

    if (parent == NULL) return false;
    if (!path_contains(root_path, parent)) {
        free(parent);
        parent = copy_range(root_path, strlen(root_path));
        if (parent == NULL) return false;
    }

    size_t parent_index = SIZE_MAX;
    bool success = ensure_folder(tree, old_tree, root_path, parent, first_track,
                                 &parent_index) &&
                   add_folder(tree, old_tree, path, parent_index, first_track,
                              false, folder_index);
    free(parent);
    return success;
}

static bool append_rows(Folder_Tree *tree, char *const *track_parents,
                        size_t track_count, size_t folder_index)
{
    Folder_Tree_Folder *folder = &tree->folders[folder_index];

    if (!folder->root) {
        tree->rows[tree->row_count++] = (Folder_Tree_Row){
            .folder = true,
            .folder_index = folder_index,
        };
    }

    for (size_t track = 0; track < track_count; ++track) {
        if (strcmp(track_parents[track], folder->path) == 0) {
            tree->rows[tree->row_count++] = (Folder_Tree_Row){
                .folder_index = folder_index,
                .track_index = track,
            };
        }
    }

    for (size_t track = 0; track < track_count; ++track) {
        for (size_t child = 0; child < tree->folder_count; ++child) {
            if (tree->folders[child].parent_index == folder_index &&
                tree->folders[child].first_track == track &&
                !append_rows(tree, track_parents, track_count, child)) {
                return false;
            }
        }
    }

    return true;
}

void folder_tree_init(Folder_Tree *tree)
{
    *tree = (Folder_Tree){0};
}

void folder_tree_uninit(Folder_Tree *tree)
{
    for (size_t i = 0; i < tree->folder_count; ++i) {
        free(tree->folders[i].path);
        free(tree->folders[i].name);
    }

    free(tree->folders);
    free(tree->rows);
    *tree = (Folder_Tree){0};
}

bool folder_tree_sync(Folder_Tree *tree, const Playlist *playlist)
{
    uint64_t signature = playlist_signature(playlist);

    if (signature == tree->playlist_signature) return true;

    Folder_Tree replacement;
    folder_tree_init(&replacement);
    size_t track_count = playlist_get_count(playlist);
    char **track_parents = calloc(track_count, sizeof(*track_parents));
    char *root_path = NULL;
    bool success = track_count == 0 || track_parents != NULL;

    for (size_t track = 0; success && track < track_count; ++track) {
        track_parents[track] = parent_path(playlist_get(playlist, track));
        success = track_parents[track] != NULL;

        if (!success) break;
        if (root_path == NULL) {
            root_path =
                copy_range(track_parents[track], strlen(track_parents[track]));
            success = root_path != NULL;
            continue;
        }

        while (!path_contains(root_path, track_parents[track])) {
            char *parent = parent_path(root_path);

            if (parent == NULL || strcmp(parent, root_path) == 0) {
                free(parent);
                success = false;
                break;
            }

            free(root_path);
            root_path = parent;
        }
    }

    size_t root_index = SIZE_MAX;

    for (size_t track = 0; success && track < track_count; ++track) {
        size_t folder_index = SIZE_MAX;
        success = ensure_folder(&replacement, tree, root_path,
                                track_parents[track], track, &folder_index);
        if (success && root_index == SIZE_MAX) {
            root_index = find_folder(&replacement, root_path);
        }
    }

    if (success && track_count > 0) {
        replacement.rows = calloc(replacement.folder_count + track_count,
                                  sizeof(*replacement.rows));
        success =
            replacement.rows != NULL &&
            append_rows(&replacement, track_parents, track_count, root_index);
    }

    for (size_t track = 0; track < track_count; ++track) {
        free(track_parents == NULL ? NULL : track_parents[track]);
    }
    free(track_parents);
    free(root_path);

    if (!success) {
        folder_tree_uninit(&replacement);
        return false;
    }

    replacement.playlist_signature = signature;
    folder_tree_uninit(tree);
    *tree = replacement;
    return true;
}

size_t folder_tree_folder_count(const Folder_Tree *tree)
{
    return tree->folder_count == 0 ? 0 : tree->folder_count - 1;
}

bool folder_tree_folder_contains(const Folder_Tree *tree, size_t folder_index,
                                 size_t candidate_index)
{
    while (candidate_index < tree->folder_count) {
        if (candidate_index == folder_index) return true;
        candidate_index = tree->folders[candidate_index].parent_index;
    }

    return false;
}

static bool row_visible(const Folder_Tree *tree, Folder_Tree_Row row)
{
    size_t folder_index = row.folder
                              ? tree->folders[row.folder_index].parent_index
                              : row.folder_index;

    while (folder_index < tree->folder_count) {
        Folder_Tree_Folder *folder = &tree->folders[folder_index];

        if (!folder->root && !folder->expanded) return false;
        folder_index = folder->parent_index;
    }

    return true;
}

size_t folder_tree_visible_count(const Folder_Tree *tree)
{
    size_t count = 0;

    for (size_t i = 0; i < tree->row_count; ++i) {
        if (row_visible(tree, tree->rows[i])) ++count;
    }

    return count;
}

bool folder_tree_visible_row(const Folder_Tree *tree, size_t visible_index,
                             Folder_Tree_Row *row)
{
    size_t current = 0;

    for (size_t i = 0; i < tree->row_count; ++i) {
        if (!row_visible(tree, tree->rows[i])) continue;

        if (current++ == visible_index) {
            if (row != NULL) *row = tree->rows[i];
            return true;
        }
    }

    return false;
}

const char *folder_tree_folder_name(const Folder_Tree *tree,
                                    size_t folder_index)
{
    if (folder_index >= tree->folder_count) return NULL;
    return tree->folders[folder_index].name;
}

bool folder_tree_folder_expanded(const Folder_Tree *tree, size_t folder_index)
{
    return folder_index < tree->folder_count &&
           tree->folders[folder_index].expanded;
}

bool folder_tree_folder_is_root(const Folder_Tree *tree, size_t folder_index)
{
    return folder_index < tree->folder_count &&
           tree->folders[folder_index].root;
}

size_t folder_tree_folder_depth(const Folder_Tree *tree, size_t folder_index)
{
    if (folder_index >= tree->folder_count ||
        tree->folders[folder_index].depth == 0) {
        return 0;
    }

    return tree->folders[folder_index].depth - 1;
}

void folder_tree_toggle(Folder_Tree *tree, size_t folder_index)
{
    if (folder_index < tree->folder_count &&
        !tree->folders[folder_index].root) {
        tree->folders[folder_index].expanded =
            !tree->folders[folder_index].expanded;
    }
}
