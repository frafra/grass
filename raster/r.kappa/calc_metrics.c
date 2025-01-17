#include <stdlib.h>
#include <grass/gis.h>
#include <grass/glocale.h>
#include "kappa.h"
#include "local_proto.h"

static int longcomp(const void *aa, const void *bb);
static int collapse(long *l, int n);

void calc_metrics(void)
{
    int i, j, k;
    size_t l;
    long *clst;
    int ncat1, ncat2;
    int cndx;
    double *pi, *pj, *pii;
    double p0 = 0.0, pC = 0.0;
    double inter1 = 0.0, inter2 = 0.0;
    int a_i = 0, b_i = 0;

    metrics = (METRICS *)G_malloc(sizeof(METRICS));
    if (nstats == 0) {
        G_warning(_("Both maps have nothing in common. Check the computational "
                    "region."));
        metrics->observations = 0;
        metrics->correct = 0;
        metrics->overall_accuracy = 0.0;
        metrics->kappa = na_value;
        metrics->kappa_variance = na_value;
        return;
    }

    /* get the cat lists */
    rlst = (long *)G_calloc(nstats * 2, sizeof(long));
    clst = (long *)G_calloc(nstats, sizeof(long));
    for (l = 0; l < nstats; l++) {
        rlst[l] = Gstats[l].cats[0];
        clst[l] = Gstats[l].cats[1];
    }

    /* sort the cat lists */
    qsort(rlst, nstats, sizeof(long), longcomp);
    qsort(clst, nstats, sizeof(long), longcomp);

    /* remove repeated cats */
    ncat1 = collapse(rlst, nstats);
    ncat2 = collapse(clst, nstats);

    /* copy clst to the end of rlst, remove repeated cats, and free unused
     * memory */
    for (i = 0; i < ncat2; i++)
        rlst[ncat1 + i] = clst[i];
    qsort(rlst, ncat1 + ncat2, sizeof(long), longcomp);
    ncat = collapse(rlst, ncat1 + ncat2);
    rlst = (long *)G_realloc(rlst, ncat * sizeof(long));
    G_free(clst);

    /* fill matrix with observed counts */
    metrics->matrix = (long *)G_malloc((size_t)ncat * ncat * sizeof(long));
    for (i = 0; i < ncat * ncat; i++)
        metrics->matrix[i] = 0;
    for (l = 0; l < nstats; l++) {
        for (j = 0; j < ncat; j++)
            if (rlst[j] == Gstats[l].cats[0])
                break;
        for (k = 0; k < ncat; k++)
            if (rlst[k] == Gstats[l].cats[1])
                break;
        /* matrix: reference in columns, classification in rows */
        metrics->matrix[j * ncat + k] = Gstats[l].count;
    }

    /* Calculate marginals */
    metrics->observations = 0;
    metrics->correct = 0;
    metrics->col_sum = (long *)G_malloc(ncat * sizeof(long));
    metrics->row_sum = (long *)G_malloc(ncat * sizeof(long));
    for (cndx = 0; cndx < ncat; cndx++) {
        long t_col = 0;
        long t_row = 0;
        long x = cndx;

        for (k = 0; k < ncat; k++) {
            t_col += metrics->matrix[x];
            x += ncat;
            t_row += metrics->matrix[cndx * ncat + k];
        }
        metrics->observations += t_row;
        metrics->col_sum[cndx] = t_col;
        metrics->row_sum[cndx] = t_row;
    }
    if (metrics->observations == 0) {
        metrics->overall_accuracy = 0.0;
        metrics->kappa = na_value;
        metrics->kappa_variance = na_value;
        return;
    }

    /* Calculate kappa values */
    /* Row sum */
    pi = (double *)G_calloc(ncat, sizeof(double));
    /* Col sum */
    pj = (double *)G_calloc(ncat, sizeof(double));
    /* Correct */
    pii = (double *)G_calloc(ncat, sizeof(double));
    metrics->conditional_kappa = (double *)G_calloc(ncat, sizeof(double));
    metrics->users_accuracy = (double *)G_calloc(ncat, sizeof(double));
    metrics->producers_accuracy = (double *)G_calloc(ncat, sizeof(double));

    for (i = 0; i < ncat; i++) {
        for (l = 0; l < nstats; l++) {
            if (Gstats[l].cats[0] == rlst[i]) {
                pi[i] += Gstats[l].count;
            }

            if (Gstats[l].cats[1] == rlst[i]) {
                pj[i] += Gstats[l].count;
            }

            if ((Gstats[l].cats[0] == Gstats[l].cats[1]) &&
                (Gstats[l].cats[0] == rlst[i])) {
                pii[i] += Gstats[l].count;
            }
        }
        metrics->correct += pii[i];
    }

    metrics->overall_accuracy = 100. * metrics->correct / metrics->observations;

    /* turn observations into probabilities */
    for (i = 0; i < ncat; i++) {
        pi[i] = pi[i] / metrics->observations;
        pj[i] = pj[i] / metrics->observations;
        pii[i] = pii[i] / metrics->observations;
        if (pi[i] == 0)
            metrics->users_accuracy[i] = na_value;
        else
            metrics->users_accuracy[i] = 100 * (pii[i] / pi[i]);
        if (pj[i] == 0)
            metrics->producers_accuracy[i] = na_value;
        else
            metrics->producers_accuracy[i] = 100 * (pii[i] / pj[i]);
        /* theta 1 */
        p0 += pii[i];
        /* theta 2 */
        pC += pi[i] * pj[i];
    }
    if (pC != 1)
        metrics->kappa = (p0 - pC) / (1 - pC);
    else
        metrics->kappa = na_value;

    /* conditional user's kappa */
    for (i = 0; i < ncat; i++) {
        if (pi[i] == 0 || (pi[i] == 1 && pj[i] == 1))
            metrics->conditional_kappa[i] = na_value;
        else
            metrics->conditional_kappa[i] =
                (pii[i] - pi[i] * pj[i]) / (pi[i] - pi[i] * pj[i]);
        inter1 += pii[i] * pow(((1 - pC) - (1 - p0) * (pi[i] + pj[i])), 2.);
    }

    /* kappa variance */
    for (l = 0; l < nstats; l++) {
        if (Gstats[l].cats[0] != Gstats[l].cats[1]) {
            for (i = 0; i < ncat; i++) {
                if (Gstats[l].cats[0] == rlst[i])
                    a_i = i;
                if (Gstats[l].cats[1] == rlst[i])
                    b_i = i;
            }
            inter2 += Gstats[l].count * pow((pi[a_i] + pj[b_i]), 2.) /
                      metrics->observations;
        }
    }
    metrics->kappa_variance = (inter1 + pow((1 - p0), 2.) * inter2 -
                               pow((p0 * pC - 2 * pC + p0), 2.)) /
                              pow((1 - pC), 4.) / metrics->observations;

    G_free(pi);
    G_free(pj);
    G_free(pii);
};

/* remove repeated values */
static int collapse(long *l, int n)
{
    long *c;
    int m;

    c = l;
    m = 1;
    while (n-- > 0) {
        if (*c != *l) {
            c++;
            *c = *l;
            m++;
        }
        l++;
    }

    return m;
}

static int longcomp(const void *aa, const void *bb)
{
    const long *a = aa;
    const long *b = bb;

    if (*a < *b)
        return -1;

    return (*a > *b);
}
