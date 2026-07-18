FROM ubuntu:22.04 AS builder

RUN apt-get update && apt-get install -y cmake g++ wget

WORKDIR /app
COPY . .

# Download the single-file cpp-httplib library
RUN wget https://raw.githubusercontent.com/yhirose/cpp-httplib/master/httplib.h

RUN mkdir build && cd build && cmake .. && make -j4

FROM ubuntu:22.04

WORKDIR /app
COPY --from=builder /app/build/luau_decompiler_server .

ENV PORT=8080
EXPOSE $PORT

CMD ["./luau_decompiler_server"]
