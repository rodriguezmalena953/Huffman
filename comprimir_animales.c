#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <inttypes.h>

#define ARCHIVO_ENTRADA "animales.txt"
#define ARCHIVO_SALIDA  "animales.huf"
#define MAX_SIMBOLOS 256
#define MAX_NODOS (2 * MAX_SIMBOLOS - 1)
#define MAX_CODIGO 256

typedef struct NodoHuffman {
    int simbolo;                 /* 0..255 en hojas, -1 en nodos internos */
    uint64_t frecuencia;
    unsigned int orden;          /* desempate determinista */
    struct NodoHuffman *izq;
    struct NodoHuffman *der;
} NodoHuffman;

typedef struct {
    NodoHuffman *datos[MAX_NODOS];
    int tamanio;
} MinHeap;

typedef struct {
    char bits[MAX_CODIGO];
} CodigoHuffman;

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
        if (frecuencias[i] > 0) {
            heap_insertar(&heap, nuevo_nodo(i, frecuencias[i], orden++));
        }
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

static void generar_codigos_rec(
    NodoHuffman *nodo,
    char camino[MAX_CODIGO],
    int profundidad,
    CodigoHuffman codigos[MAX_SIMBOLOS])
{
    if (nodo == NULL)
        return;

    if (nodo->izq == NULL && nodo->der == NULL) {
        /* Caso especial: archivo con un solo símbolo. */
        if (profundidad == 0) {
            camino[0] = '0';
            profundidad = 1;
        }

        memcpy(codigos[nodo->simbolo].bits, camino, (size_t)profundidad);
        codigos[nodo->simbolo].bits[profundidad] = '\0';
        return;
    }

    camino[profundidad] = '0';
    generar_codigos_rec(nodo->izq, camino, profundidad + 1, codigos);

    camino[profundidad] = '1';
    generar_codigos_rec(nodo->der, camino, profundidad + 1, codigos);
}

static void liberar_arbol(NodoHuffman *nodo)
{
    if (nodo == NULL)
        return;
    liberar_arbol(nodo->izq);
    liberar_arbol(nodo->der);
    free(nodo);
}

static int escribir_u16(FILE *f, uint16_t v)
{
    unsigned char b[2] = {
        (unsigned char)(v & 0xFFu),
        (unsigned char)((v >> 8) & 0xFFu)
    };
    return fwrite(b, 1, 2, f) == 2;
}

static int escribir_u64(FILE *f, uint64_t v)
{
    unsigned char b[8];
    for (int i = 0; i < 8; i++)
        b[i] = (unsigned char)((v >> (8 * i)) & 0xFFu);
    return fwrite(b, 1, 8, f) == 8;
}

static int escribir_cabecera(
    FILE *salida,
    uint64_t tamanio_original,
    const uint64_t frecuencias[MAX_SIMBOLOS])
{
    if (fwrite("HUF1", 1, 4, salida) != 4)
        return 0;

    if (!escribir_u64(salida, tamanio_original))
        return 0;

    uint16_t cantidad = 0;
    for (int i = 0; i < MAX_SIMBOLOS; i++) {
        if (frecuencias[i] > 0)
            cantidad++;
    }

    if (!escribir_u16(salida, cantidad))
        return 0;

    for (int i = 0; i < MAX_SIMBOLOS; i++) {
        if (frecuencias[i] > 0) {
            unsigned char simbolo = (unsigned char)i;
            if (fwrite(&simbolo, 1, 1, salida) != 1)
                return 0;
            if (!escribir_u64(salida, frecuencias[i]))
                return 0;
        }
    }

    return 1;
}

static int escribir_datos_comprimidos(
    FILE *entrada,
    FILE *salida,
    const CodigoHuffman codigos[MAX_SIMBOLOS])
{
    unsigned char byte_salida = 0;
    int bits_usados = 0;
    int c;

    while ((c = fgetc(entrada)) != EOF) {
        const char *codigo = codigos[(unsigned char)c].bits;

        for (int i = 0; codigo[i] != '\0'; i++) {
            byte_salida = (unsigned char)((byte_salida << 1) | (codigo[i] == '1' ? 1u : 0u));
            bits_usados++;

            if (bits_usados == 8) {
                if (fwrite(&byte_salida, 1, 1, salida) != 1)
                    return 0;
                byte_salida = 0;
                bits_usados = 0;
            }
        }
    }

    /* Relleno con ceros al final si el último byte quedó incompleto. */
    if (bits_usados > 0) {
        byte_salida <<= (8 - bits_usados);
        if (fwrite(&byte_salida, 1, 1, salida) != 1)
            return 0;
    }

    return 1;
}

int main(void)
{
    uint64_t frecuencias[MAX_SIMBOLOS] = {0};
    CodigoHuffman codigos[MAX_SIMBOLOS] = {{{0}}};
    char camino[MAX_CODIGO];
    uint64_t tamanio_original = 0;

    FILE *entrada = fopen(ARCHIVO_ENTRADA, "rb");
    if (entrada == NULL) {
        fprintf(stderr, "Error: no se pudo abrir %s\n", ARCHIVO_ENTRADA);
        return EXIT_FAILURE;
    }

    int c;
    while ((c = fgetc(entrada)) != EOF) {
        frecuencias[(unsigned char)c]++;
        tamanio_original++;
    }

    NodoHuffman *raiz = construir_arbol(frecuencias);
    if (raiz != NULL)
        generar_codigos_rec(raiz, camino, 0, codigos);

    FILE *salida = fopen(ARCHIVO_SALIDA, "wb");
    if (salida == NULL) {
        fprintf(stderr, "Error: no se pudo crear %s\n", ARCHIVO_SALIDA);
        fclose(entrada);
        liberar_arbol(raiz);
        return EXIT_FAILURE;
    }

    if (!escribir_cabecera(salida, tamanio_original, frecuencias)) {
        fprintf(stderr, "Error escribiendo la cabecera del archivo comprimido.\n");
        fclose(entrada);
        fclose(salida);
        liberar_arbol(raiz);
        return EXIT_FAILURE;
    }

    rewind(entrada);

    if (tamanio_original > 0 && !escribir_datos_comprimidos(entrada, salida, codigos)) {
        fprintf(stderr, "Error escribiendo los datos comprimidos.\n");
        fclose(entrada);
        fclose(salida);
        liberar_arbol(raiz);
        return EXIT_FAILURE;
    }

    fclose(entrada);
    fclose(salida);
    liberar_arbol(raiz);

    FILE *medir = fopen(ARCHIVO_SALIDA, "rb");
    long tamanio_comprimido = -1;
    if (medir != NULL) {
        fseek(medir, 0, SEEK_END);
        tamanio_comprimido = ftell(medir);
        fclose(medir);
    }

    printf("Compresion Huffman terminada.\n");
    printf("Entrada : %s\n", ARCHIVO_ENTRADA);
    printf("Salida  : %s\n", ARCHIVO_SALIDA);
    printf("Tamano original   : %" PRIu64 " bytes\n", tamanio_original);
    if (tamanio_comprimido >= 0) {
        printf("Tamano comprimido : %ld bytes\n", tamanio_comprimido);
        if (tamanio_original > 0) {
            printf("Tamano final      : %.2f %% del original\n",
                   100.0 * (double)tamanio_comprimido / (double)tamanio_original);
        }
    }

    return EXIT_SUCCESS;
}
