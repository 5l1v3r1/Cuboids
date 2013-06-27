#include "heuristic_index.h"
#include "arguments/parser.h"
#include "heuristic/heuristic_io.h"
#include "search/cuboid.h"
#include <stdio.h>

static HeuristicIndex * heuristicIndex = NULL;
static IndexerArguments arguments;
static CSSearchContext * searchContext;
static pthread_mutex_t globalMutex = PTHREAD_MUTEX_INITIALIZER;
static int currentDepth = 0;
static volatile long long nodesAdded = 0;
static const char * fileName;

CLArgumentList * subproblem_default_arguments(const char * spName);

void print_usage(const char * name);
CLArgumentList * process_arguments(int argc, const char * argv[]);

int generate_heuristic(const char * name, CLArgumentList * args);
int run_search();

CSCallbacks generate_callbacks();

void indexer_handle_progress(void * data);
void indexer_handle_depth(void * data, int len);
int indexer_accepts_sequence(void * data, const int * sequence, int len, int depthRem);
int indexer_accepts_cuboid(void * data, const Cuboid * cuboid, Cuboid * cache, int depthRem);
void indexer_handle_cuboid(void * data, const Cuboid * cuboid, Cuboid * cache,
                           const int * sequence, int len);
void indexer_handle_save_data(void * data, CSSearchState * save);
void indexer_handle_finished(void * data);

int main(int argc, const char * argv[]) {
    if (argc < 3) {
        print_usage(argv[0]);
        return 1;
    }
    fileName = argv[2];
    
    IndexerArguments args;
    CLArgumentList * list = process_arguments(argc, argv);
    if (!list) return 1;
    if (!indexer_process_arguments(list, &arguments)) {
        fprintf(stderr, "error: failed to process arguments.\n");
        cl_argument_list_free(list);
        return 1;
    }
    
    indexer_remove_all_defaults(list);
    int result = generate_heuristic(argv[1], list);
    cl_argument_list_free(list);
    if (!result) {
        alg_list_release(arguments.operations);
        fprintf(stderr, "error: failed to create heuristic.\n");
        return 1;
    }
    result = run_search();
    if (!result) {
        fprintf(stderr, "error: failed to launch search.\n");
        alg_list_release(arguments.operations);
        heuristic_index_free(heuristicIndex);
        return 1;
    }
    return 0;
}

CLArgumentList * subproblem_default_arguments(const char * spName) {
    int entryCount = sizeof(HSubproblemTable) / sizeof(HSubproblem);
    int i;
    for (i = 0; i < entryCount; i++) {
        if (strcmp(HSubproblemTable[i].name, spName) == 0) {
            return HSubproblemTable[i].default_arguments();
        }
    }
    return NULL;
}

void print_usage(const char * name) {
    fprintf(stderr, "Usage: %s <index type> <output> [--maxdepth=n] [--threads=n]\n\
       [--operations ...] [--symmetries xyz]\n\n", name);
    fflush(stderr);
}

CLArgumentList * process_arguments(int argc, const char * argv[]) {
    CLArgumentList * spDefs = subproblem_default_arguments(argv[1]);
    if (!spDefs) {
        fprintf(stderr, "Error: invalid indexer `%s`.\n", argv[1]);
        return NULL;
    }
    
    CLArgumentList * defaults = indexer_default_arguments();
    cl_argument_list_add_all(defaults, spDefs);
    cl_argument_list_free(spDefs);
    
    int failIndex;
    CLArgumentList * result = cl_parse_arguments(&argv[3], argc - 3, defaults, &failIndex);
    cl_argument_list_free(defaults);
    if (!result) {
        fprintf(stderr, "error: invalid argument near `%s`\n", argv[3 + failIndex]);
    }
    return result;
}

/*********************
 * Commencing action *
 *********************/

int generate_heuristic(const char * name, CLArgumentList * args) {
    heuristicIndex = heuristic_index_create(args, arguments, name);
    return (heuristicIndex != NULL);
}

int run_search() {
    CSCallbacks cbs = generate_callbacks();
    CSSettings settings;
    settings.rootNode = cuboid_create(arguments.symmetries.dims);
    settings.algorithms = arguments.operations;
    settings.cacheCuboid = 1;
    BSSettings bsSettings;
    bsSettings.threadCount = arguments.threadCount;
    bsSettings.minDepth = 0;
    bsSettings.maxDepth = arguments.maxDepth;
    bsSettings.nodeInterval = 1000000;
    
    searchContext = cs_run(settings, bsSettings, cbs);
    
    while (1) {
        sleep(1);
    }
    
    return 1;
}

/*************
 * Callbacks *
 *************/

CSCallbacks generate_callbacks() {
    CSCallbacks cbs;
    cbs.handle_progress = indexer_handle_progress;
    cbs.handle_depth = indexer_handle_depth;
    cbs.accepts_sequence = indexer_accepts_sequence;
    cbs.accepts_cuboid = indexer_accepts_cuboid;
    cbs.handle_cuboid = indexer_handle_cuboid;
    cbs.handle_save_data = indexer_handle_save_data;
    cbs.handle_finished = indexer_handle_finished;
    return cbs;
}

void indexer_handle_progress(void * data) {
    pthread_mutex_lock(&globalMutex);
    long long value = __sync_fetch_and_or(&nodesAdded, 0);
    printf("Found %lld sequences [depth %d, %d cosets, %d angles].\n", 
           value, currentDepth, heuristicIndex->heuristic->cosetCount,
           heuristicIndex->heuristic->angles->numDistinct);
    pthread_mutex_unlock(&globalMutex);
}

void indexer_handle_depth(void * data, int len) {
    pthread_mutex_lock(&globalMutex);
    printf("Exploring depth of %d.\n", len);
    currentDepth = len;
    pthread_mutex_unlock(&globalMutex);
}

int indexer_accepts_sequence(void * data, const int * sequence, int len, int depthRem) {
    return 1;
}

int indexer_accepts_cuboid(void * data, const Cuboid * cuboid, Cuboid * cache, int depthRem) {
    if (arguments.threadCount > 1) {
        pthread_mutex_lock(&globalMutex);
    }
    int depth = currentDepth - depthRem;
    int flag = heuristic_index_accepts_node(heuristicIndex, depth, currentDepth,
                                            cuboid, cache);
    if (arguments.threadCount > 1) {
        pthread_mutex_unlock(&globalMutex);
    }

    return flag;
}

void indexer_handle_cuboid(void * data, const Cuboid * cuboid, Cuboid * cache,
                           const int * sequence, int len) {
    if (arguments.threadCount > 1) {
        pthread_mutex_lock(&globalMutex);
    }
    int added = heuristic_index_add_node(heuristicIndex, cuboid, cache, len);
    if (arguments.threadCount > 1) {
        pthread_mutex_unlock(&globalMutex);
    }
    if (added) {
        __sync_fetch_and_add(&nodesAdded, 1);
    }
}

void indexer_handle_save_data(void * data, CSSearchState * save) {
    puts("handle_save_data should never be called!");
    abort();
}

void indexer_handle_finished(void * data) {
    cs_context_release(searchContext);
    
    puts("Writing to output file...");
    FILE * fp = fopen(fileName, "w");
    save_heuristic(heuristicIndex->heuristic, fp);
    fclose(fp);
    heuristic_index_free(heuristicIndex);
    
    exit(0);
}
