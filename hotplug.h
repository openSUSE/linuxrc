int hotplug_main(int argc, char** argv);
int hotplug_wait_for_event(char* type);
int hotplug_wait_for_path(char* path);
char* hotplug_get_info(char* key);
void hotplug_event_handled(void);
