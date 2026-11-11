#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CAPIClient CAPIClient;

CAPIClient* capi_client_create(
    const char* host,
    uint16_t port,
    const char* library_directory
);

int capi_client_connect(CAPIClient* client);
int capi_client_send_text(CAPIClient* client, const char* message, int fast);
int capi_client_send_image(CAPIClient* client, const char* path, int raw);
int capi_client_send_video(CAPIClient* client, const char* path, int checksum);
int capi_client_receive_event(
    CAPIClient* client, int* event_type,
    char* buffer, size_t capacity, int timeout_ms
);

/* Returns received byte count, -2 on timeout, or -1 on error.
   The output buffer is null-terminated when capacity permits. */
int capi_client_receive(
    CAPIClient* client,
    char* buffer,
    size_t capacity,
    int timeout_ms
);

void capi_client_close(CAPIClient* client);
void capi_client_destroy(CAPIClient* client);
const char* capi_last_error(void);

#ifdef __cplusplus
}
#endif
