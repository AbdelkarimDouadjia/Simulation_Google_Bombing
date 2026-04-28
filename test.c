#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define ALPHA 0.85
#define EPS 1e-12
#define MAX_IT 10000

typedef enum {
    ATTACK_COMPLETE,
    ATTACK_RING,
    ATTACK_ISOLATED
} AttackType;

typedef struct {
    int n;
    unsigned char **adj;
} Graph;

typedef struct {
    int node;
    double pr;
} RankEntry;

typedef struct {
    int iterations;
} PageRankResult;

static const char *attack_name(AttackType type) {
    switch (type) {
        case ATTACK_COMPLETE: return "complet";
        case ATTACK_RING: return "anneau";
        case ATTACK_ISOLATED: return "isoles";
        default: return "inconnu";
    }
}

static void *xmalloc(size_t size) {
    void *p = malloc(size);
    if (p == NULL) {
        fprintf(stderr, "Erreur allocation memoire\n");
        exit(EXIT_FAILURE);
    }
    return p;
}

static void *xcalloc(size_t count, size_t size) {
    void *p = calloc(count, size);
    if (p == NULL) {
        fprintf(stderr, "Erreur allocation memoire\n");
        exit(EXIT_FAILURE);
    }
    return p;
}

static Graph create_graph(int n) {
    Graph g;
    g.n = n;
    g.adj = xmalloc((size_t)n * sizeof(unsigned char *));
    for (int i = 0; i < n; i++) {
        g.adj[i] = xcalloc((size_t)n, sizeof(unsigned char));
    }
    return g;
}

static void free_graph(Graph *g) {
    if (g == NULL || g->adj == NULL) return;
    for (int i = 0; i < g->n; i++) {
        free(g->adj[i]);
    }
    free(g->adj);
    g->adj = NULL;
    g->n = 0;
}

static void add_edge(Graph *g, int from, int to) {
    if (from >= 0 && from < g->n && to >= 0 && to < g->n && from != to) {
        g->adj[from][to] = 1;
    }
}

static Graph read_graph(const char *filename) {
    FILE *f = fopen(filename, "r");
    if (f == NULL) {
        fprintf(stderr, "Erreur ouverture fichier %s\n", filename);
        exit(EXIT_FAILURE);
    }

    int n, n2, m;
    if (fscanf(f, "%d %d %d", &n, &n2, &m) != 3) {
        fprintf(stderr, "Erreur lecture premiere ligne\n");
        fclose(f);
        exit(EXIT_FAILURE);
    }
    if (n != n2) {
        fprintf(stderr, "Attention: dimensions non carrees (%d, %d)\n", n, n2);
    }

    Graph g = create_graph(n);
    for (int k = 0; k < m; k++) {
        int i, j;
        if (fscanf(f, "%d %d", &i, &j) != 2) {
            fprintf(stderr, "Erreur lecture arc %d\n", k + 1);
            fclose(f);
            free_graph(&g);
            exit(EXIT_FAILURE);
        }
        add_edge(&g, i - 1, j - 1);
    }

    fclose(f);
    return g;
}

static Graph copy_with_attack(const Graph *base, int target, int attackers, AttackType type) {
    int old_n = base->n;
    Graph g = create_graph(old_n + attackers);

    for (int i = 0; i < old_n; i++) {
        memcpy(g.adj[i], base->adj[i], (size_t)old_n * sizeof(unsigned char));
    }

    int first_attacker = old_n;

    if (type == ATTACK_COMPLETE) {
        for (int i = 0; i < attackers; i++) {
            int u = first_attacker + i;
            for (int j = 0; j < attackers; j++) {
                int v = first_attacker + j;
                if (u != v) add_edge(&g, u, v);
            }
            add_edge(&g, u, target);
        }
    } else if (type == ATTACK_RING) {
        if (attackers == 1) {
            add_edge(&g, target, first_attacker);
            add_edge(&g, first_attacker, target);
        } else {
            add_edge(&g, target, first_attacker);
            for (int i = 0; i < attackers - 1; i++) {
                add_edge(&g, first_attacker + i, first_attacker + i + 1);
            }
            add_edge(&g, first_attacker + attackers - 1, target);
        }
    } else if (type == ATTACK_ISOLATED) {
        for (int i = 0; i < attackers; i++) {
            add_edge(&g, first_attacker + i, target);
        }
    }

    return g;
}

static PageRankResult pagerank(const Graph *g, double *pr) {
    int n = g->n;
    double *next = xcalloc((size_t)n, sizeof(double));
    int *out = xcalloc((size_t)n, sizeof(int));

    for (int i = 0; i < n; i++) {
        pr[i] = 1.0 / n;
        for (int j = 0; j < n; j++) {
            if (g->adj[i][j]) out[i]++;
        }
    }

    int iter = 0;
    double diff = 0.0;
    do {
        double dangling_mass = 0.0;
        for (int i = 0; i < n; i++) {
            next[i] = (1.0 - ALPHA) / n;
            if (out[i] == 0) dangling_mass += pr[i];
        }

        double dangling_share = ALPHA * dangling_mass / n;
        for (int j = 0; j < n; j++) {
            next[j] += dangling_share;
        }

        for (int i = 0; i < n; i++) {
            if (out[i] == 0) continue;
            double contribution = ALPHA * pr[i] / out[i];
            for (int j = 0; j < n; j++) {
                if (g->adj[i][j]) next[j] += contribution;
            }
        }

        diff = 0.0;
        for (int i = 0; i < n; i++) {
            diff += fabs(next[i] - pr[i]);
            pr[i] = next[i];
        }
        iter++;
    } while (diff > EPS && iter < MAX_IT);

    free(next);
    free(out);

    PageRankResult result;
    result.iterations = iter;
    return result;
}

static int compare_rank_desc(const void *a, const void *b) {
    const RankEntry *ra = (const RankEntry *)a;
    const RankEntry *rb = (const RankEntry *)b;
    if (ra->pr < rb->pr) return 1;
    if (ra->pr > rb->pr) return -1;
    return ra->node - rb->node;
}

static const char *target_kind(int index) {
    if (index == 0) return "forte";
    if (index == 1) return "moyenne";
    return "faible";
}

int main(void) {
    const char *filename = "Harvard500.txt";
    const int sizes[] = {1, 2, 5, 10, 20, 50, 100, 200};
    const int nb_sizes = (int)(sizeof(sizes) / sizeof(sizes[0]));
    const AttackType attacks[] = {ATTACK_COMPLETE, ATTACK_RING, ATTACK_ISOLATED};
    const int nb_attacks = (int)(sizeof(attacks) / sizeof(attacks[0]));

    Graph base = read_graph(filename);
    double *base_pr = xmalloc((size_t)base.n * sizeof(double));
    PageRankResult base_result = pagerank(&base, base_pr);

    RankEntry *ranking = xmalloc((size_t)base.n * sizeof(RankEntry));
    for (int i = 0; i < base.n; i++) {
        ranking[i].node = i;
        ranking[i].pr = base_pr[i];
    }
    qsort(ranking, (size_t)base.n, sizeof(RankEntry), compare_rank_desc);

    int targets[3];
    targets[0] = ranking[0].node;
    targets[1] = ranking[base.n / 2].node;
    targets[2] = ranking[base.n - 1].node;

    FILE *csv = fopen("bombing_results.csv", "w");
    if (csv == NULL) {
        fprintf(stderr, "Erreur creation bombing_results.csv\n");
        free(ranking);
        free(base_pr);
        free_graph(&base);
        return EXIT_FAILURE;
    }

    fprintf(csv, "type_cible,id_cible,rank_initial,pagerank_initial,type_attaque,nb_attaquants,pagerank_apres,augmentation,ratio,iterations\n");

    printf("Graphe initial: %d pages, alpha=%.2f, convergence=%d iterations\n\n", base.n, ALPHA, base_result.iterations);
    printf("Cibles choisies avec le PageRank initial:\n");
    for (int t = 0; t < 3; t++) {
        int rank_position = (t == 0) ? 1 : (t == 1 ? base.n / 2 + 1 : base.n);
        printf("- cible %s: page %d, rang %d, PageRank %.12f\n",
               target_kind(t), targets[t] + 1, rank_position, base_pr[targets[t]]);
    }
    printf("\nSimulation en cours...\n");

    for (int t = 0; t < 3; t++) {
        int target = targets[t];
        int rank_position = (t == 0) ? 1 : (t == 1 ? base.n / 2 + 1 : base.n);
        for (int a = 0; a < nb_attacks; a++) {
            for (int s = 0; s < nb_sizes; s++) {
                int attackers = sizes[s];
                Graph attacked = copy_with_attack(&base, target, attackers, attacks[a]);
                double *pr = xmalloc((size_t)attacked.n * sizeof(double));
                PageRankResult result = pagerank(&attacked, pr);
                double before = base_pr[target];
                double after = pr[target];
                double increase = after - before;
                double ratio = (before > 0.0) ? after / before : 0.0;

                fprintf(csv, "%s,%d,%d,%.15f,%s,%d,%.15f,%.15f,%.6f,%d\n",
                        target_kind(t), target + 1, rank_position, before,
                        attack_name(attacks[a]), attackers, after, increase, ratio,
                        result.iterations);

                free(pr);
                free_graph(&attacked);
            }
        }
    }

    fclose(csv);

    printf("Resultats ecrits dans bombing_results.csv\n\n");
    printf("Lecture rapide: comparer la colonne ratio (> 1 signifie que la cible a gagne du PageRank).\n");

    free(ranking);
    free(base_pr);
    free_graph(&base);

    return EXIT_SUCCESS;
}
