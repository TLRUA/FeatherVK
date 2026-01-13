#version 450

layout(location = 0) flat in int objectId;
layout(location = 0) out int outObjectId;

void main() {
    outObjectId = objectId;
}