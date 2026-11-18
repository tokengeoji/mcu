#ifndef SITE_PROFILE_H
#define SITE_PROFILE_H

/* Shared map coordinates: +x right, +y down, millimetres. No surveyed values
   were supplied. Fill this table from measurements before the model trial. */
typedef struct {
    int x_mm, y_mm;             /* sensor origin in the model */
    int dx, dy;                 /* (1,0), (-1,0), (0,1), or (0,-1) */
    int length_mm, offset_mm;   /* allowed along-track interval 0..length */
    int background_mm, margin_mm; /* target must be closer by at least margin */
} SiteCorridor;

static const SiteCorridor site_corridors[4] = {
    {0,0,0,0,0,0,0,0}, /* S1 */
    {0,0,0,0,0,0,0,0}, /* S2 */
    {0,0,0,0,0,0,0,0}, /* S3 */
    {0,0,0,0,0,0,0,0}  /* S4 */
};

/* Enter the verified usable range of the five installed sensors. RAW always
   exposes measured pulse-derived distances, even before this is configured. */
#define SITE_RANGE_MIN_MM        0
#define SITE_RANGE_MAX_MM        0
#define SITE_HAZARD_ON_MM        0
#define SITE_HAZARD_OFF_MM       0
#define SITE_HAZARD_SAMPLES      0  /* consecutive qualifying readings */
#define SITE_MAX_SPEED_MM_S      0  /* model's measured maximum travel speed */
#define SITE_POSITION_ERROR_MM   0  /* single measurement error bound */
#define SITE_SERVO_CLOSED_US     0
#define SITE_SERVO_OPEN_US       0
#endif
