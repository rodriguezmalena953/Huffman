#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <inttypes.h>

#define ARCHIVO_ENTRADA "animales.huf"
#define ARCHIVO_SALIDA  "animales_recuperado.txt"
#define MAX_SIMBOLOS 256
#define MAX_NODOS (2 * MAX_SIMBOLOS - 1)

typedef struct NodoHuffman {
    int simbolo;
    uint64_t frecuencia;
    unsigned int orden;
    struct NodoHuffman *izq;
    struct NodoHuffman *der;
} NodoHuffman;

typedef struct {
    NodoHuffman *datos[MAX_NODOS];
    int tamanio;
} MinHeap;

static int menor(const NodoHuffman *a, const NodoHuffman *b)
{
    if (a->frecuencia != b->frecuencia)
        return a->frecuencia < b->frecuencia;
    return a->orden < b->orden;
}

static void heap_insertar(MinHeap *heap, NodoHuffman *nodo)
{
    int i = heap->tamanio++;
    heap->datos[i] = nodo;

    while (i > 0) {
        int padre = (i - 1) / 2;
        if (!menor(heap->datos[i], heap->datos[padre]))
            break;

        NodoHuffman *tmp = heap->datos[i];
        heap->datos[i] = heap->datos[padre];
        heap->datos[padre] = tmp;
        i = padre;
    }
}

static NodoHuffman *heap_extraer_min(MinHeap *heap)
{
    if (heap->tamanio == 0)
        return NULL;

    NodoHuffman *minimo = heap->datos[0];
    heap->tamanio--;

    if (heap->tamanio > 0) {
        heap->datos[0] = heap->datos[heap->tamanio];
        int i = 0;

        while (1) {
            int izq = 2 * i + 1;
            int der = 2 * i + 2;
            int menor_hijo = i;

            if (izq < heap->tamanio && menor(heap->datos[izq], heap->datos[menor_hijo]))
                menor_hijo = izq;
            if (der < heap->tamanio && menor(heap->datos[der], heap->datos[menor_hijo]))
                menor_hijo = der;

            if (menor_hijo == i)
                break;

            NodoHuffman *tmp = heap->datos[i];
            heap->datos[i] = heap->datos[menor_hijo];
            heap->datos[menor_hijo] = tmp;
            i = menor_hijo;
        }
    }

    return minimo;
}

static NodoHuffman *nuevo_nodo(int simbolo, uint64_t frecuencia, unsigned int orden)
{
    NodoHuffman *nodo = (NodoHuffman *)malloc(sizeof(NodoHuffman));
    if (nodo == NULL) {
        fprintf(stderr, "Error: no se pudo reservar memoria.\n");
        exit(EXIT_FAILURE);
    }

    nodo->simbolo = simbolo;
    nodo->frecuencia = frecuencia;
    nodo->orden = orden;
    nodo->izq = NULL;
    nodo->der = NULL;
    return nodo;
}

static NodoHuffman *construir_arbol(const uint64_t frecuencias[MAX_SIMBOLOS])
{
    MinHeap heap = { .tamanio = 0 };
    unsigned int orden = 0;

    for (int i = 0; i < MAX_SIMBOLOS; i++) {
        if (frecuencias[i] > 0)
            heap_insertar(&heap, nuevo_nodo(i, frecuencias[i], orden++));
    }

    if (heap.tamanio == 0)
        return NULL;

    while (heap.tamanio > 1) {
        NodoHuffman *a = heap_extraer_min(&heap);
        NodoHuffman *b = heap_extraer_min(&heap);

        NodoHuffman *padre = nuevo_nodo(
            -1,
            a->frecuencia + b->frecuencia,
            orden++
        );
        padre->izq = a;
        padre->der = b;
        heap_insertar(&heap, padre);
    }

    return heap_extraer_min(&heap);
}

static void liberar_arbol(NodoHuffman *nodo)
{
    if (nodo == NULL)
        return;
    liberar_arbol(nodo->izq);
    liberar_arbol(nodo->der);
    free(nodo);
}

static int leer_u16(FILE *f, uint16_t *v)
{
    unsigned char b[2];
    if (fread(b, 1, 2, f) != 2)
        return 0;

    *v = (uint16_t)b[0] | ((uint16_t)b[1] << 8);
    return 1;
}

static int leer_u64(FILE *f, uint64_t *v)
{
    unsigned char b[8];
    if (fread(b, 1, 8, f) != 8)
        return 0;

    uint64_t resultado = 0;
    for (int i = 0; i < 8; i++)
        resultado |= ((uint64_t)b[i]) << (8 * i);

    *v = resultado;
    return 1;
}

static int leer_cabecera(
    FILE *entrada,
    uint64_t *tamanio_original,
    uint64_t frecuencias[MAX_SIMBOLOS])
{
    unsigned char magic[4];
    if (fread(magic, 1, 4, entrada) != 4)
        return 0;

    if (memcmp(magic, "HUF1", 4) != 0) {
        fprintf(stderr, "Error: el archivo no tiene formato HUF1.\n");
        return 0;
    }

    if (!leer_u64(entrada, tamanio_original))
        return 0;

    uint16_t cantidad = 0;
    if (!leer_u16(entrada, &cantidad))
        return 0;

    if (cantidad > MAX_SIMBOLOS)
        return 0;

    for (int i = 0; i < MAX_SIMBOLOS; i++)
        frecuencias[i] = 0;

    for (uint16_t i = 0; i < cantidad; i++) {
        unsigned char simbolo;
        uint64_t frecuencia;

        if (fread(&simbolo, 1, 1, entrada) != 1)
            return 0;
        if (!leer_u64(entrada, &frecuencia))
            return 0;
        if (frecuencia == 0)
            return 0;

        frecuencias[simbolo] = frecuencia;
    }

    /* Validar que las frecuencias sumen el tamaño original. */
    uint64_t suma = 0;
    for (int i = 0; i < MAX_SIMBOLOS; i++)
        suma += frecuencias[i];

    return suma == *tamanio_original;
}

static int es_hoja(const NodoHuffman *nodo)
{
    return nodo != NULL && nodo->izq == NULL && nodo->der == NULL;
}

static int descomprimir_datos(
    FILE *entrada,
    FILE *salida,
    NodoHuffman *raiz,
    uint64_t tamanio_original)
{
    if (tamanio_original == 0)
        return 1;

    if (raiz == NULL)
        return 0;

    /* Caso especial: todo el archivo era un único byte repetido. */
    if (es_hoja(raiz)) {
        for (uint64_t i = 0; i < tamanio_original; i++) {
            if (fputc(raiz->simbolo, salida) == EOF)
                return 0;
        }
        return 1;
    }

    uint64_t escritos = 0;
    NodoHuffman *actual = raiz;
    int c;

    while (escritos < tamanio_original && (c = fgetc(entrada)) != EOF) {
        unsigned char byte = (unsigned char)c;

        for (int bit = 7; bit >= 0 && escritos < tamanio_original; bit--) {
            int valor_bit = (byte >> bit) & 1;
            actual = valor_bit ? actual->der : actual->izq;

            if (actual == NULL)
                return 0;

            if (es_hoja(actual)) {
                if (fputc(actual->simbolo, salida) == EOF)
                    return 0;
                escritos++;
                actual = raiz;
            }
        }
    }

    return escritos == tamanio_original;
}

int main(void)
{
    uint64_t frecuencias[MAX_SIMBOLOS] = {0};
    uint64_t tamanio_original = 0;

    FILE *entrada = fopen(ARCHIVO_ENTRADA, "rb");
    if (entrada == NULL) {
        fprintf(stderr, "Error: no se pudo abrir %s\n", ARCHIVO_ENTRADA);
        return EXIT_FAILURE;
    }

    if (!leer_cabecera(entrada, &tamanio_original, frecuencias)) {
        fprintf(stderr, "Error: cabecera Huffman invalida.\n");
        fclose(entrada);
        return EXIT_FAILURE;
    }

    NodoHuffman *raiz = construir_arbol(frecuencias);

    FILE *salida = fopen(ARCHIVO_SALIDA, "wb");
    if (salida == NULL) {
        fprintf(stderr, "Error: no se pudo crear %s\n", ARCHIVO_SALIDA);
        fclose(entrada);
        liberar_arbol(raiz);
        return EXIT_FAILURE;
    }

    if (!descomprimir_datos(entrada, salida, raiz, tamanio_original)) {
        fprintf(stderr, "Error: datos comprimidos corruptos o incompletos.\n");
        fclose(entrada);
        fclose(salida);
        liberar_arbol(raiz);
        return EXIT_FAILURE;
    }

    fclose(entrada);
    fclose(salida);
    liberar_arbol(raiz);

    printf("Descompresion Huffman terminada.\n");
    printf("Entrada : %s\n", ARCHIVO_ENTRADA);
    printf("Salida  : %s\n", ARCHIVO_SALIDA);
    printf("Bytes recuperados: %" PRIu64 "\n", tamanio_original);

    return EXIT_SUCCESS;
}
