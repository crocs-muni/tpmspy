#pragma once
#ifndef SINK_H
#define SINK_H

#include "sinks.h"
#include "sockspy.h"
#include "strings.h"

[[nodiscard]]
void *sink_open(struct strings *args, const struct context *ctx);

bool sink_send(sink_obj obj, const struct sink_message *msg);

bool sink_close(sink_obj obj);

#endif // SINK_H
