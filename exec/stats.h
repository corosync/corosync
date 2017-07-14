cs_error_t stats_map_init(const struct corosync_api_v1 *api);

cs_error_t stats_map_get(const char *key_name,
		   void *value,
		   size_t *value_len,
		   icmap_value_types_t *type);

cs_error_t stats_map_set(const char *key_name,
		   const void *value,
		   size_t value_len,
		   icmap_value_types_t type);

cs_error_t stats_map_adjust_int(const char *key_name, int32_t step);

cs_error_t stats_map_delete(const char *key_name);

int stats_map_is_key_ro(const char *key_name);

icmap_iter_t stats_map_iter_init(const char *prefix);
const char *stats_map_iter_next(icmap_iter_t iter, size_t *value_len, icmap_value_types_t *type);
void stats_map_iter_finalize(icmap_iter_t iter);

cs_error_t stats_map_track_add(const char *key_name,
			 int32_t track_type,
			 icmap_notify_fn_t notify_fn,
			 void *user_data,
			 icmap_track_t *icmap_track);

cs_error_t stats_map_track_delete(icmap_track_t icmap_track);
void *stats_map_track_get_user_data(icmap_track_t icmap_track);

void stats_trigger_trackers(void);
