#ifndef PLAYLIST_SEARCH_H
#define PLAYLIST_SEARCH_H

#include <stdbool.h>
#include <stddef.h>

#include "playlist.h"

#define PLAYLIST_SEARCH_QUERY_CAPACITY 256

typedef struct {
    size_t track_index;
    int score;
} Playlist_Search_Result;

typedef struct {
    char query[PLAYLIST_SEARCH_QUERY_CAPACITY];
    size_t cursor;
    Playlist_Search_Result *results;
    size_t count;
    size_t capacity;
} Playlist_Search;

void playlist_search_uninit(Playlist_Search *search);
void playlist_search_clear(Playlist_Search *search);
bool playlist_search_append(Playlist_Search *search, const char *utf8,
                            size_t size);
bool playlist_search_backspace(Playlist_Search *search);
bool playlist_search_delete(Playlist_Search *search);
bool playlist_search_delete_to_start(Playlist_Search *search);
bool playlist_search_delete_to_end(Playlist_Search *search);
bool playlist_search_delete_previous_word(Playlist_Search *search);
void playlist_search_cursor_start(Playlist_Search *search);
void playlist_search_cursor_end(Playlist_Search *search);
void playlist_search_cursor_left(Playlist_Search *search);
void playlist_search_cursor_right(Playlist_Search *search);
void playlist_search_cursor_word_left(Playlist_Search *search);
void playlist_search_cursor_word_right(Playlist_Search *search);
bool playlist_search_refresh(Playlist_Search *search,
                             const Playlist *playlist);
bool playlist_search_active(const Playlist_Search *search);
size_t playlist_search_count(const Playlist_Search *search,
                             const Playlist *playlist);
size_t playlist_search_track(const Playlist_Search *search,
                             size_t result_index);

#endif
