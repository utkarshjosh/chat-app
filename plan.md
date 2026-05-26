Technical Plan: Simple Chat Application
1. Project Goal

Build a simple UI-based chat application with:

HTML/CSS/JavaScript frontend
C-based backend server
Username-based user identification
Chat messages stored in browser localStorage
Simple send/receive chat flow

This matches the project direction of using C for backend logic and HTML/CSS/JS for the frontend interface.

2. System Architecture
Browser UI
HTML + CSS + JavaScript
        |
        | HTTP Requests
        |
C Server
Handles users and messages
        |
        | Temporary runtime storage
        |
Browser localStorage
Stores username and chat history locally
3. Main Components
A. Frontend UI

The frontend will be built using:

index.html

It will contain:

Username input screen
Chat window
Message input box
Send button
Chat message display area

The UI should remain simple and clean.

B. JavaScript Logic

JavaScript will handle:

Saving username in localStorage
Sending messages to the C server
Fetching latest messages from the C server
Displaying messages in the chat window
Saving received messages in localStorage

Example localStorage usage:

chatUsername
chatMessages
C. C Backend Server

The C server will handle basic HTTP routes.

Main server file:

server.c

Server responsibilities:

Serve the HTML page
Receive messages from users
Store messages temporarily in memory
Send all messages back to connected browsers
Identify each message using the username sent from frontend
4. API Design
Route 1: Load UI
GET /

Purpose:

Sends index.html to browser
Route 2: Send Message
POST /send

Purpose:

Receives message from frontend

Data format:

username: message

Example:

Vinay: Hello everyone
Route 3: Get Messages
GET /messages

Purpose:

Sends all current messages to frontend

Response example:

[
  "Vinay: Hello",
  "Sarvagya: Hi"
]
5. Data Storage Plan
Browser Side

Use localStorage for:

Data	Purpose
chatUsername	Stores logged-in username
chatMessages	Stores local copy of chat history

This ensures that if the page refreshes, the username and recent messages remain visible.

Server Side

Use a simple C array buffer:

messages[100][512]

Purpose:

Store latest 100 messages during server runtime
Keep implementation simple
No database required
6. User Identification Flow
User opens the chat app.
User enters username.
Username is saved in browser localStorage.
Every message is sent with the username.
Chat window displays messages with sender name.

Example:

Vinay: Hello
Tanmay: Hi
7. Message Flow
User types message
        |
JavaScript reads username
        |
Message sent to C server using POST /send
        |
C server stores message in memory
        |
Frontend calls GET /messages every 1 second
        |
Messages displayed in browser
        |
Messages saved in localStorage
8. Features to Implement
Basic Features
Username login
Send message
Receive message
Display sender name
Store chat in localStorage
Show old messages after refresh
Optional Simple Features
Clear chat button
Logout/change username
Different color for own messages
Enter key to send message
9. Files Required
chat-application/
│
├── server.c        // C backend server
└── index.html      // UI + JavaScript
10. Testing Plan
Test Case	Expected Result
Open app in browser	Chat UI loads
Enter username	Username is saved
Send message	Message appears in chat
Open second tab	Second user can chat
Refresh page	Messages reload from localStorage
Empty message	Message should not send
Long message	Should be handled safely
11. Final Deliverables
server.c
index.html
Working local chat application
Simple technical explanation
Screenshots of UI
Final project report section explaining frontend, backend, and storage flow


