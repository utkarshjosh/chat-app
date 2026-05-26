# Campus Echo

A lightweight chat project built with plain HTML, CSS, JavaScript, and a small server powered by C language.

This version keeps chat messages only in server memory. The browser does not store chat history. Each user has a fixed server-side retention limit, defined by a constant in the backend, so only the most recent messages for that user are kept.

## Overview

The application is designed to stay simple and easy to explain for a school project:

- no database
- no frontend framework
- no external backend library
- no browser-based message storage

The frontend is a single HTML file. The backend is a single C server file that serves the page and exposes a small HTTP API.

## Features

- username-based chat
- simple single-page interface
- server-side in-memory message storage
- fixed per-user message retention limit
- automatic message refresh every second
- message send with Enter key
- blocking first-run username modal
- timestamps in chat history
- left/right chat bubble layout
- ping sound for new incoming messages
- live server stats in the UI
- simple GET APIs for messages, users, and server stats

## Tech Stack

### Frontend

- HTML
- CSS
- Vanilla JavaScript

### Backend

- C
- POSIX sockets
- Basic HTTP request parsing

## Architecture

```text
Browser UI
    |
    | HTTP requests
    v
C Server
    |
    | in-memory message buffer
    v
Retained per-user messages
```

## File Structure

```text
sompal/
├── build_server.bat  # Windows build script
├── index.html   # frontend UI and browser logic
├── server.c     # C backend and API
├── plan.md      # original implementation plan
└── README.md    # documentation
```

## Storage Model

### Browser

The browser only stores the username locally:

| Key | Purpose |
| --- | --- |
| `chatUsername` | keeps the saved username between refreshes |

The browser does not store chat messages.

### Server

The server stores messages in memory using a static array of `ChatMessage` records.

Important constants in `server.c`:

```c
#define MAX_TOTAL_MESSAGES 500
#define MAX_MESSAGES_PER_USER 10
```

What this means:

- the server can hold up to 500 messages total
- each user can keep only their latest 10 messages
- when a user sends more than the limit, that user's oldest retained message is removed
- all messages are lost when the server stops

## Application Flow

### Username Flow

1. The user enters a username.
2. The username is saved in browser `localStorage`.
3. The username is attached to each new message sent to the server.
4. If no username is saved, the app shows a blocking modal before the chat can be used.

### Sending a Message

1. The frontend sends `POST /send`.
2. The server validates the username and message.
3. The server stores the message in memory.
4. The server enforces the per-user retention limit.

### Receiving Messages

1. The frontend polls `GET /messages`.
2. The server returns the retained messages as JSON objects.
3. The frontend renders the results directly from the server response.
4. A short sound plays when a new message arrives from another user.

## API Documentation

Base URL:

```text
http://localhost:8080
```

---

### `GET /`

Returns the frontend application page.

#### Response

- Status: `200 OK`
- Content-Type: `text/html; charset=utf-8`

#### Example

```bash
curl -i http://localhost:8080/
```

---

### `POST /send`

Stores a new message in server memory.

#### Request Content-Type

```text
application/x-www-form-urlencoded
```

#### Request Parameters

| Field | Type | Required | Description |
| --- | --- | --- | --- |
| `username` | string | yes | sender name |
| `message` | string | yes | message text |

#### Example Request

```bash
curl -i -X POST \
  -d "username=Vinay&message=Hello+everyone" \
  http://localhost:8080/send
```

#### Success Response

- Status: `200 OK`
- Content-Type: `application/json`

```json
{
  "ok": true,
  "stored": {
    "id": 1,
    "username": "Vinay",
    "message": "Hello everyone",
    "timestamp": "2026-05-26 18:42:10"
  }
}
```

#### Error Response

- Status: `400 Bad Request`
- Content-Type: `application/json`

```json
{
  "ok": false,
  "error": "Username and message are required."
}
```

---

### `GET /messages`

Returns retained chat messages from server memory.

#### Query Parameters

| Parameter | Type | Required | Description |
| --- | --- | --- | --- |
| `user` | string | no | return only messages for one user |
| `limit` | number | no | return only the latest N matching messages |

#### Example Request

```bash
curl -i http://localhost:8080/messages
```

#### Example Filtered Request

```bash
curl -i "http://localhost:8080/messages?user=Vinay&limit=5"
```

#### Example Response

```json
[
  {
    "id": 1,
    "username": "Vinay",
    "message": "Hello",
    "timestamp": "2026-05-26 18:42:10"
  },
  {
    "id": 2,
    "username": "Sarvagya",
    "message": "Hi",
    "timestamp": "2026-05-26 18:42:22"
  }
]
```

---

### `GET /users`

Returns the list of users that currently have retained messages on the server.

#### Example Request

```bash
curl -i http://localhost:8080/users
```

#### Example Response

```json
[
  {
    "username": "Vinay",
    "messageCount": 3
  },
  {
    "username": "Sarvagya",
    "messageCount": 2
  }
]
```

---

### `GET /stats`

Returns high-level server memory information.

#### Example Request

```bash
curl -i http://localhost:8080/stats
```

#### Example Response

```json
{
  "totalMessages": 5,
  "maxTotalMessages": 500,
  "maxMessagesPerUser": 10
}
```

## API Notes

- The API is intentionally minimal.
- There is no authentication.
- Messages are stored only in memory.
- Usernames and messages are trimmed before storing.
- Empty usernames or empty messages are rejected.
- Message IDs increase over time during one server run.

## Build and Run

### Compile

```bash
cc -std=c11 -Wall -Wextra -pedantic server.c -o server
```

### Compile on Windows

Use the batch script from Command Prompt:

```bat
build_server.bat
```

Notes:

- the Windows build expects `gcc` or `clang` in `PATH`
- MinGW-w64 is the simplest setup for Windows
- the script produces `server.exe`

### Start the Server

```bash
./server
```

Expected console output:

```text
Chat server running on http://localhost:8080
Server keeps the latest 10 messages per user in memory.
```

### Open the App

Visit:

```text
http://localhost:8080
```

## Usage

1. Compile and start the server.
2. Open the app in a browser.
3. Save a username.
4. Send messages.
5. Open a second tab or window to test another user.

## Testing Checklist

- App loads in the browser
- Username stays saved after refresh
- Browser refresh fetches messages from the server
- Empty messages are rejected
- `GET /messages` returns JSON objects
- `GET /users` returns active users
- `GET /stats` returns retention settings
- A user sending more than the limit keeps only their latest retained messages

## Limitations

- no permanent database
- no authentication
- no HTTPS
- no private chat
- no chat rooms
- no persistent history after server shutdown

These tradeoffs keep the project small, predictable, and easy to present.

## Summary

This project demonstrates a complete chat application with:

- a browser-based UI
- a C backend server
- server-only message storage
- per-user bounded retention in memory
- simple HTTP APIs for reading and writing chat data

It fits a school project well because the codebase is compact, the architecture is clear, and the data flow is easy to explain.
