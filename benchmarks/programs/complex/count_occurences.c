#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
	char* str;
	size_t length;
	size_t capacity;
} String;

size_t String_hash(const String* str) {
	size_t hash = 0;
	for (size_t i = 0; i < str->length; i++) {
		hash = str->str[i] + (hash << 6) + (hash << 16) - hash;
	}
	return hash;
}

String String_clone(const String* str) {
	String clone = {
	    .str      = strndup(str->str, str->length),
	    .length   = str->length,
	    .capacity = str->length,
	};
	return clone;
}

String from_trimmed(const char* str) {
	// Trim whitespace, punctuation, etc.
	size_t start = 0;
	size_t end   = strlen(str);
	while (start < end && !isalnum(str[start])) {
		start++;
	}
	while (end > start && !isalnum(str[end - 1])) {
		end--;
	}
	// Copy the trimmed string
	String result = {
	    .str      = strndup(str + start, end - start),
	    .length   = end - start,
	    .capacity = end - start,
	};
	return result;
}

void String_free(String str) {
	free(str.str);
}

typedef struct {
	String str;
	size_t nb_occurences;
} WordOccurence;

typedef struct {
	WordOccurence* data;
	size_t size;
	size_t capacity;
} WordOccurenceList;

WordOccurenceList WordOccurenceList_create(size_t capacity) {
	WordOccurenceList list;
	list.data     = (WordOccurence*)malloc(capacity * sizeof(WordOccurence));
	list.size     = 0;
	list.capacity = capacity;
	return list;
}

void WordOccurenceList_push(WordOccurenceList* list, const WordOccurence occurence) {
	if (list->size == list->capacity) {
		list->capacity *= 2;
		list->data = (WordOccurence*)realloc(list->data, list->capacity * sizeof(WordOccurence));
	}
	list->data[list->size] = occurence;
	list->size++;
}

void WordOccurenceList_insert_or_increment(WordOccurenceList* list, const String str) {
	for (size_t i = 0; i < list->size; i++) {
		if (strcmp(list->data[i].str.str, str.str) == 0) {
			list->data[i].nb_occurences++;
			String_free(str);
			return;
		}
	}
	WordOccurence occurence = {
	    .str	   = str,
	    .nb_occurences = 1,
	};
	WordOccurenceList_push(list, occurence);
}

void WordOccurenceList_free(WordOccurenceList list) {
	for (size_t i = 0; i < list.size; i++) {
		String_free(list.data[i].str);
	}
	free(list.data);
}

typedef struct {
	WordOccurenceList* buckets;
	size_t nb_buckets;
} HashTable;

HashTable HashTable_create(size_t nb_buckets) {
	HashTable table;
	table.buckets	 = (WordOccurenceList*)malloc(nb_buckets * sizeof(WordOccurenceList));
	table.nb_buckets = nb_buckets;
	for (size_t i = 0; i < nb_buckets; i++) {
		table.buckets[i] = WordOccurenceList_create(10);
	}
	return table;
}

void HashTable_insert(HashTable* table, const String word) {
	size_t hash		  = String_hash(&word) % table->nb_buckets;
	WordOccurenceList* bucket = &table->buckets[hash];
	WordOccurenceList_insert_or_increment(bucket, word);
}

void HashTable_free(HashTable table) {
	for (size_t i = 0; i < table.nb_buckets; i++) {
		for (size_t j = 0; j < table.buckets[i].size; j++) {
			String_free(table.buckets[i].data[j].str);
		}
		free(table.buckets[i].data);
	}
	free(table.buckets);
}

int compare_word_occurences(const void* a, const void* b) {
	const WordOccurence* occurence_a = (const WordOccurence*)a;
	const WordOccurence* occurence_b = (const WordOccurence*)b;
	return occurence_b->nb_occurences - occurence_a->nb_occurences;
}

WordOccurenceList HashTable_sort_entries(const HashTable* table) {
	WordOccurenceList list = WordOccurenceList_create(10);
	for (size_t i = 0; i < table->nb_buckets; i++) {
		WordOccurenceList* bucket = &table->buckets[i];
		for (size_t j = 0; j < bucket->size; j++) {
			WordOccurenceList_push(&list,
					       (WordOccurence){
						   .str		  = String_clone(&bucket->data[j].str),
						   .nb_occurences = bucket->data[j].nb_occurences,
					       });
		}
	}
	qsort(list.data, list.size, sizeof(WordOccurence), compare_word_occurences);
	return list;
}

bool is_only_whitespace(const char* str) {
	while (*str != '\0') {
		if (!isspace(*str)) {
			return false;
		}
		str++;
	}
	return true;
}

int main() {
	// Load the file poems.txt
	FILE* file = fopen("benchmarks/programs/complex/poems.txt", "r");
	if (file == NULL) {
		fprintf(stderr, "Could not open file\n");
		exit(EXIT_FAILURE);
	}

	// Create a hash table
	HashTable table = HashTable_create(100);

	// Read the file line by line
	char* line = NULL;
	size_t len = 0;
	while (getline(&line, &len, file) != -1) {
		// Split the line into words
		char* word = strtok(line, " ");
		while (word != NULL) {
			if (is_only_whitespace(word)) {
				word = strtok(NULL, " ");
				continue;
			}
			String str = from_trimmed(word);
			HashTable_insert(&table, str);
			word = strtok(NULL, " ");
		}
	}

	// Sort the entries by number of occurences
	WordOccurenceList sorted_entries    = HashTable_sort_entries(&table);
	volatile String* most_occuring_word = &sorted_entries.data[0].str;

	HashTable_free(table);
	WordOccurenceList_free(sorted_entries);
	free(line);
	fclose(file);
	exit(EXIT_SUCCESS);
}