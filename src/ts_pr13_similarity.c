#include "tapesister/pr13.h"

static float clamp01(float value)
{
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

float ts_pr13_family_similarity(TsFamilyRelation relation, float mutation)
{
    float departure = 0.0f;
    mutation = clamp01(mutation);
    if (relation == TS_FAMILY_CHILD) departure = 0.50f;
    else if (relation == TS_FAMILY_COUSIN) departure = 0.75f;
    else if (relation == TS_FAMILY_STRANGER) departure = 0.95f;
    return 1.0f - mutation * departure;
}
