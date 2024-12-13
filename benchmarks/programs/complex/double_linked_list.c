/**
 * @file benchmarks/programs/complex/double_linked_list.c
 * @brief A program that manipulates a double linked list
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

// Structure for a node in the double linked list
typedef struct Node {
	uint64_t data;
	struct Node* prev;
	struct Node* next;
} Node;

// Structure for the double linked list
typedef struct LinkedList {
	Node* head;
	Node* tail;
} LinkedList;

// Function to create a new node
Node* create_node(uint64_t data) {
	Node* new_node = (Node*)malloc(sizeof(Node));
	new_node->data = data;
	new_node->prev = NULL;
	new_node->next = NULL;
	return new_node;
}

// Function to create a new double linked list
LinkedList create_linked_list() {
	return (LinkedList){
	    .head = NULL,
	    .tail = NULL,
	};
}

void free_list(LinkedList list) {
	Node* current = list.head;
	while (current != NULL) {
		Node* next = current->next;
		free(current);
		current = next;
	}
}

// Function to insert a node at the beginning of the double linked list
void insertFirst(LinkedList* list, uint64_t data) {
	Node* new_node = create_node(data);
	if (list->head == NULL) {
		list->head = new_node;
		list->tail = new_node;
	}
	else {
		new_node->next	 = list->head;
		list->head->prev = new_node;
		list->head	 = new_node;
	}
}

// Function to insert a node at the end of the double linked list
void insert_last(LinkedList* list, uint64_t data) {
	Node* new_node = create_node(data);
	if (list->head == NULL) {
		list->head = new_node;
		list->tail = new_node;
	}
	else {
		new_node->prev	 = list->tail;
		list->tail->next = new_node;
		list->tail	 = new_node;
	}
}

// Function to delete a node from the double linked list
void delete_node(LinkedList* list, Node* node) {
	if (node == list->head) {
		list->head = node->next;
	}
	else if (node == list->tail) {
		list->tail = node->prev;
	}
	else {
		node->prev->next = node->next;
		node->next->prev = node->prev;
	}
	free(node);
}

// Function to pop a node at a given index from the double linked list
void pop_at_index(LinkedList* list, int index) {
	if (index < 0) {
		printf("Invalid index\n");
		return;
	}
	Node* current_node = list->head;
	int current_index  = 0;
	while (current_node != NULL && current_index < index) {
		current_node = current_node->next;
		current_index++;
	}
	if (current_node == NULL) {
		printf("Index out of range\n");
		return;
	}
	delete_node(list, current_node);
}

void insert_at_index(LinkedList* list, uint64_t data, int index) {
	if (index < 0) {
		printf("Invalid index\n");
		return;
	}
	if (index == 0) {
		insertFirst(list, data);
		return;
	}
	Node* current_node = list->head;
	int current_index  = 0;
	while (current_node != NULL && current_index < index - 1) {
		current_node = current_node->next;
		current_index++;
	}
	if (current_node == NULL) {
		printf("Index out of range\n");
		return;
	}
	Node* new_node = create_node(data);
	new_node->prev = current_node;
	new_node->next = current_node->next;
	if (current_node->next != NULL) {
		current_node->next->prev = new_node;
	}
	current_node->next = new_node;
}

int main() {
	LinkedList list = create_linked_list();

	const int NB_ELEMS = 200000;
	for (int i = 0; i < NB_ELEMS; i++) {
		insert_last(&list, i);
	}

	const int elems_to_insert[] = {105, 86, 178200, 2, 989, 2678, 4413, 118697, 70000, 45000};
	for (int i = 0; i < sizeof(elems_to_insert) / sizeof(int); i++) {
		insert_at_index(&list, elems_to_insert[i], elems_to_insert[i]);
	}

	const int indexes_to_pop[] = {72908, 9995, 53, 2794, 99142, 111982, 1746, 6662, 2};
	for (int i = 0; i < sizeof(indexes_to_pop) / sizeof(int); i++) {
		pop_at_index(&list, indexes_to_pop[i]);
	}

	free_list(list);

	return 0;
}