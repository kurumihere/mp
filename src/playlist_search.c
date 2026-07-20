#include "playlist_search.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <glib.h>

static bool reserve_results(Playlist_Search *search, size_t capacity)
{
    if (capacity <= search->capacity) return true;
    if (capacity > SIZE_MAX / sizeof(*search->results)) return false;

    size_t grown = search->capacity == 0 ? 32 : search->capacity;

    while (grown < capacity) {
        if (grown > SIZE_MAX / 2) {
            grown = capacity;
            break;
        }

        grown *= 2;
    }

    Playlist_Search_Result *results =
        realloc(search->results, grown * sizeof(*results));

    if (results == NULL) return false;

    search->results = results;
    search->capacity = grown;
    return true;
}

static char *fold_text(const char *text)
{
    char *folded = g_utf8_casefold(text, -1);

    if (folded == NULL) return NULL;

    char *normalized = g_utf8_normalize(folded, -1, G_NORMALIZE_ALL);
    g_free(folded);
    return normalized;
}

static bool fuzzy_score(const char *query, const char *candidate, int *score)
{
    const char *query_cursor = query;
    const char *candidate_cursor = candidate;
    gunichar previous = 0;
    int total = 0;
    int streak = 0;
    int position = 0;

    while (*query_cursor != '\0') {
        gunichar wanted = g_utf8_get_char(query_cursor);
        query_cursor = g_utf8_next_char(query_cursor);
        int gap = 0;
        bool found = false;

        while (*candidate_cursor != '\0') {
            gunichar current = g_utf8_get_char(candidate_cursor);
            candidate_cursor = g_utf8_next_char(candidate_cursor);

            if (current == wanted) {
                bool boundary = previous == 0 || !g_unichar_isalnum(previous);
                streak = gap == 0 ? streak + 1 : 1;
                total += 100 + streak * 24 + (boundary ? 42 : 0) - gap * 4;
                previous = current;
                ++position;
                found = true;
                break;
            }

            previous = current;
            ++gap;
            ++position;
        }

        if (!found) return false;
    }

    *score = total - position;
    return true;
}

static int compare_results(const void *left_value, const void *right_value)
{
    const Playlist_Search_Result *left = left_value;
    const Playlist_Search_Result *right = right_value;

    if (left->score != right->score) {
        return left->score > right->score ? -1 : 1;
    }

    if (left->track_index == right->track_index) return 0;
    return left->track_index < right->track_index ? -1 : 1;
}

void playlist_search_uninit(Playlist_Search *search)
{
    if (search == NULL) return;

    free(search->results);
    *search = (Playlist_Search){0};
}

void playlist_search_clear(Playlist_Search *search)
{
    if (search == NULL) return;

    search->query[0] = '\0';
    search->cursor = 0;
    search->count = 0;
}

bool playlist_search_append(Playlist_Search *search, const char *utf8,
                            size_t size)
{
    if (search == NULL || utf8 == NULL || size == 0) return false;

    size_t length = strlen(search->query);

    if (size >= sizeof(search->query) - length) return false;

    memmove(search->query + search->cursor + size,
            search->query + search->cursor,
            length - search->cursor + 1);
    memcpy(search->query + search->cursor, utf8, size);
    search->cursor += size;
    return true;
}

bool playlist_search_backspace(Playlist_Search *search)
{
    if (search == NULL || search->cursor == 0) return false;

    char *end = search->query + search->cursor;
    char *previous = g_utf8_find_prev_char(search->query, end);

    if (previous == NULL) return false;

    size_t previous_offset = (size_t)(previous - search->query);
    memmove(previous, end, strlen(end) + 1);
    search->cursor = previous_offset;
    return true;
}

bool playlist_search_delete(Playlist_Search *search)
{
    if (search == NULL) return false;

    size_t length = strlen(search->query);

    if (search->cursor >= length) return false;

    const char *next = g_utf8_next_char(search->query + search->cursor);
    memmove(search->query + search->cursor, next, strlen(next) + 1);
    return true;
}

bool playlist_search_delete_to_start(Playlist_Search *search)
{
    if (search == NULL || search->cursor == 0) return false;

    memmove(search->query, search->query + search->cursor,
            strlen(search->query + search->cursor) + 1);
    search->cursor = 0;
    return true;
}

bool playlist_search_delete_to_end(Playlist_Search *search)
{
    if (search == NULL || search->query[search->cursor] == '\0') return false;

    search->query[search->cursor] = '\0';
    return true;
}

static bool query_character_is_space(const char *character)
{
    return g_unichar_isspace(g_utf8_get_char(character));
}

void playlist_search_cursor_start(Playlist_Search *search)
{
    if (search != NULL) search->cursor = 0;
}

void playlist_search_cursor_end(Playlist_Search *search)
{
    if (search != NULL) search->cursor = strlen(search->query);
}

void playlist_search_cursor_left(Playlist_Search *search)
{
    if (search == NULL || search->cursor == 0) return;

    char *previous =
        g_utf8_find_prev_char(search->query, search->query + search->cursor);

    if (previous != NULL) search->cursor = (size_t)(previous - search->query);
}

void playlist_search_cursor_right(Playlist_Search *search)
{
    if (search == NULL || search->query[search->cursor] == '\0') return;

    search->cursor = (size_t)(g_utf8_next_char(search->query + search->cursor) -
                              search->query);
}

void playlist_search_cursor_word_left(Playlist_Search *search)
{
    if (search == NULL) return;

    while (search->cursor > 0) {
        char *previous =
            g_utf8_find_prev_char(search->query,
                                  search->query + search->cursor);

        if (previous == NULL || !query_character_is_space(previous)) break;
        search->cursor = (size_t)(previous - search->query);
    }

    while (search->cursor > 0) {
        char *previous =
            g_utf8_find_prev_char(search->query,
                                  search->query + search->cursor);

        if (previous == NULL || query_character_is_space(previous)) break;
        search->cursor = (size_t)(previous - search->query);
    }
}

void playlist_search_cursor_word_right(Playlist_Search *search)
{
    if (search == NULL) return;

    while (search->query[search->cursor] != '\0' &&
           query_character_is_space(search->query + search->cursor)) {
        playlist_search_cursor_right(search);
    }

    while (search->query[search->cursor] != '\0' &&
           !query_character_is_space(search->query + search->cursor)) {
        playlist_search_cursor_right(search);
    }
}

bool playlist_search_delete_previous_word(Playlist_Search *search)
{
    if (search == NULL || search->cursor == 0) return false;

    size_t end = search->cursor;
    playlist_search_cursor_word_left(search);
    memmove(search->query + search->cursor, search->query + end,
            strlen(search->query + end) + 1);
    return true;
}

bool playlist_search_refresh(Playlist_Search *search,
                             const Playlist *playlist)
{
    if (search == NULL || playlist == NULL) return false;

    search->count = 0;

    if (search->query[0] == '\0') return true;

    char *query = fold_text(search->query);

    if (query == NULL) return false;

    char **tokens = g_strsplit_set(query, " \t\r\n", -1);

    if (tokens == NULL) {
        g_free(query);
        return false;
    }

    size_t track_count = playlist_get_count(playlist);

    for (size_t index = 0; index < track_count; ++index) {
        const Track_Metadata *metadata = playlist_get_metadata(playlist, index);
        const char *path = playlist_get(playlist, index);
        char *candidate;

        if (metadata != NULL) {
            candidate = g_strdup_printf("%s %s %s %s", metadata->title,
                                        metadata->artist, metadata->album,
                                        path == NULL ? "" : path);
        } else {
            candidate = g_strdup(path == NULL ? "" : path);
        }

        char *folded_candidate =
            candidate == NULL ? NULL : fold_text(candidate);
        g_free(candidate);

        if (folded_candidate == NULL) {
            g_strfreev(tokens);
            g_free(query);
            return false;
        }

        int score = 0;
        bool matches = true;

        for (size_t token = 0; tokens[token] != NULL; ++token) {
            if (tokens[token][0] == '\0') continue;

            int token_score = 0;

            if (!fuzzy_score(tokens[token], folded_candidate, &token_score)) {
                matches = false;
                break;
            }

            score += token_score;
        }

        g_free(folded_candidate);

        if (!matches) continue;

        if (!reserve_results(search, search->count + 1)) {
            g_strfreev(tokens);
            g_free(query);
            return false;
        }

        search->results[search->count++] = (Playlist_Search_Result){
            .track_index = index,
            .score = score,
        };
    }

    g_strfreev(tokens);
    g_free(query);
    qsort(search->results, search->count, sizeof(*search->results),
          compare_results);
    return true;
}

bool playlist_search_active(const Playlist_Search *search)
{
    return search != NULL && search->query[0] != '\0';
}

size_t playlist_search_count(const Playlist_Search *search,
                             const Playlist *playlist)
{
    return playlist_search_active(search) ? search->count
                                          : playlist_get_count(playlist);
}

size_t playlist_search_track(const Playlist_Search *search,
                             size_t result_index)
{
    return playlist_search_active(search)
               ? search->results[result_index].track_index
               : result_index;
}
