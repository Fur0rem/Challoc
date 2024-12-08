#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
	size_t node;
	int distance;
} HeapNode;

typedef struct {
	size_t capacity;
	size_t size;
	HeapNode* data;
} MinHeap;

MinHeap* create_min_heap(size_t capacity) {
	MinHeap* heap  = (MinHeap*)malloc(sizeof(MinHeap));
	heap->capacity = capacity;
	heap->size     = 0;
	heap->data     = (HeapNode*)malloc(capacity * sizeof(HeapNode));
	return heap;
}

void free_min_heap(MinHeap* heap) {
	free(heap->data);
	free(heap);
}

void swap(HeapNode* a, HeapNode* b) {
	HeapNode temp = *a;
	*a	      = *b;
	*b	      = temp;
}

void min_heapify(MinHeap* heap, size_t idx) {
	size_t smallest = idx;
	size_t left	= 2 * idx + 1;
	size_t right	= 2 * idx + 2;

	if (left < heap->size && heap->data[left].distance < heap->data[smallest].distance) {
		smallest = left;
	}

	if (right < heap->size && heap->data[right].distance < heap->data[smallest].distance) {
		smallest = right;
	}

	if (smallest != idx) {
		swap(&heap->data[smallest], &heap->data[idx]);
		min_heapify(heap, smallest);
	}
}

HeapNode extract_min(MinHeap* heap) {
	if (heap->size == 0) {
		return (HeapNode){.node = -1, .distance = INT_MAX};
	}

	HeapNode root = heap->data[0];
	heap->data[0] = heap->data[heap->size - 1];
	heap->size--;
	min_heapify(heap, 0);

	return root;
}

void decrease_key(MinHeap* heap, size_t node, int distance) {
	size_t i;
	for (i = 0; i < heap->size; i++) {
		if (heap->data[i].node == node) {
			heap->data[i].distance = distance;
			break;
		}
	}

	while (i != 0 && heap->data[(i - 1) / 2].distance > heap->data[i].distance) {
		swap(&heap->data[i], &heap->data[(i - 1) / 2]);
		i = (i - 1) / 2;
	}
}

bool is_in_min_heap(MinHeap* heap, size_t node) {
	for (size_t i = 0; i < heap->size; i++) {
		if (heap->data[i].node == node) {
			return true;
		}
	}
	return false;
}

typedef struct {
	size_t node;
	size_t distance;
} Neighbor;

typedef struct {
	size_t capacity;
	size_t size;
	Neighbor* data;
} Neighbors;

Neighbors* create_neighbors(size_t capacity) {
	Neighbors* neighbors = (Neighbors*)malloc(sizeof(Neighbors));
	neighbors->capacity  = capacity;
	neighbors->size	     = 0;
	neighbors->data	     = (Neighbor*)malloc(capacity * sizeof(Neighbor));
	return neighbors;
}

void free_neighbors(Neighbors* neighbors) {
	free(neighbors->data);
	free(neighbors);
}

void add_neighbor(Neighbors* neighbors, size_t node, size_t distance) {
	if (neighbors->size == neighbors->capacity) {
		neighbors->capacity *= 2;
		neighbors->data = (Neighbor*)realloc(neighbors->data, neighbors->capacity * sizeof(Neighbor));
	}
	neighbors->data[neighbors->size].node	  = node;
	neighbors->data[neighbors->size].distance = distance;
	neighbors->size++;
}

typedef struct {
	size_t capacity;
	size_t size;
	Neighbors** data;
} Graph;

Graph* create_graph(size_t capacity) {
	Graph* graph	= (Graph*)malloc(sizeof(Graph));
	graph->capacity = capacity;
	graph->size	= 0;
	graph->data	= (Neighbors**)malloc(capacity * sizeof(Neighbors*));
	for (size_t i = 0; i < capacity; i++) {
		graph->data[i] = create_neighbors(10); // Initialize each node's neighbors list
	}
	return graph;
}

void free_graph(Graph* graph) {
	for (size_t i = 0; i < graph->capacity; i++) {
		free_neighbors(graph->data[i]);
	}
	free(graph->data);
	free(graph);
}

void add_edge(Graph* graph, size_t node1, size_t node2, size_t distance) {
	if (node1 >= graph->capacity || node2 >= graph->capacity) {
		return;
	}
	add_neighbor(graph->data[node1], node2, distance);
}

void dijkstra(Graph* graph, size_t start, size_t end, size_t** min_distances) {
	int* distances	  = (int*)malloc(graph->capacity * sizeof(int));
	bool* visited	  = (bool*)malloc(graph->capacity * sizeof(bool));
	MinHeap* min_heap = create_min_heap(graph->capacity);

	for (size_t i = 0; i < graph->capacity; i++) {
		distances[i]		   = INT_MAX;
		visited[i]		   = false;
		min_heap->data[i].node	   = i;
		min_heap->data[i].distance = INT_MAX;
	}

	distances[start] = 0;
	decrease_key(min_heap, start, 0);
	min_heap->size = graph->capacity;

	while (min_heap->size > 0) {
		HeapNode min_node = extract_min(min_heap);
		size_t u	  = min_node.node;

		if (u == -1 || distances[u] == INT_MAX) {
			break;
		}

		visited[u]		= true;
		min_distances[start][u] = distances[u];

		for (size_t i = 0; i < graph->data[u]->size; i++) {
			size_t v   = graph->data[u]->data[i].node;
			int weight = graph->data[u]->data[i].distance;

			if (!visited[v] && distances[u] != INT_MAX && distances[u] + weight < distances[v]) {
				distances[v] = distances[u] + weight;
				decrease_key(min_heap, v, distances[v]);
			}
		}
	}

	size_t result = distances[end];
	free(distances);
	free(visited);
	free_min_heap(min_heap);

	min_distances[start][end] = result;
}

int hash(int key, int capacity) {
	return (key ^ (key >> 8)) | (key - 5) % capacity;
}

int main() {
	const int NB_NODES = 500;
	Graph* graph	   = create_graph(NB_NODES);

	for (int i = 0; i < NB_NODES; i++) {
		for (int j = 0; j < NB_NODES; j++) {
			add_edge(graph, i + j, hash(i, NB_NODES), j + 1);
			add_edge(graph, i, hash(j, NB_NODES), i + 1);
			add_edge(graph, hash(i & j, NB_NODES), hash(j, NB_NODES), hash(i, 128) + 1);
		}
	}

	size_t** min_distances = (size_t**)malloc(NB_NODES * sizeof(size_t*));
	for (int i = 0; i < NB_NODES; i++) {
		min_distances[i] = (size_t*)calloc(NB_NODES - i, sizeof(size_t));
	}

	for (int i = 0; i < NB_NODES; i++) {
		for (int j = 0; j < NB_NODES - i; j++) {
			if (min_distances[i][j] == 0) {
				dijkstra(graph, i, j, min_distances);
			}
		}
	}

	volatile size_t all_distances = 0;
	for (int i = 0; i < NB_NODES; i++) {
		for (int j = 0; j < NB_NODES - i; j++) {
			all_distances += min_distances[i][j];
		}
	}

	free_graph(graph);
	for (int i = 0; i < NB_NODES; i++) {
		free(min_distances[i]);
	}
	free(min_distances);

	return 0;
}
