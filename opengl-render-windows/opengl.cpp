﻿#include <algorithm> // For std::min
#include <codecvt>
#include <EGL/egl.h>
#include <filesystem>
#include <filesystem>
#include <GLES2/gl2.h>
#include <iostream>
#include <iostream>
#include <locale>
#include <regex>
#include <sstream>
#include <string>
#include <sys/stat.h>
#define STB_TRUETYPE_IMPLEMENTATION  // force following include to generate implementation
#include "stb_easyfont.h"
#include <chrono>

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <stdio.h>
#include <fcntl.h>
#include "miniz.h"
#include <time.h>

//GLES setup
GLuint programObject;
GLuint programObjectTextRender;
EGLDisplay eglDisplay;
EGLConfig eglConfig;
EGLSurface eglSurface;
EGLContext eglContext;

// VNC shaders
// Vertex shader source
const char* vertexShaderSource =
"#version 100\n" // Specify ES 2.0 version
"attribute vec2 position;    \n"
"attribute vec2 texCoord;     \n" // Add texture coordinate attribute
"varying vec2 v_texCoord;     \n" // Declare varying variable for texture coordinate
"void main()                  \n"
"{                            \n"
"   gl_Position = vec4(position, 0.0, 1.0); \n"
"   v_texCoord = texCoord;   \n"
"   gl_PointSize = 4.0;      \n" // Point size
"}                            \n";

const char* fragmentShaderSource =
"#version 100\n" // Specify ES 2.0 version
"precision highp float;\n"
"varying vec2 v_texCoord;\n"
"uniform sampler2D texture;\n"
"void main()\n"
"{\n"
"    gl_FragColor = texture2D(texture, v_texCoord);\n"
"}\n";

// Text Rendering shaders
const char* vertexShaderSourceText =
"attribute vec2 position;    \n"
"void main()                  \n"
"{                            \n"
"   gl_Position = vec4(position, 0.0, 1.0); \n"
"   gl_PointSize = 4.0;      \n" // Point size
"}                            \n";

// Fragment shader source
const char* fragmentShaderSourceText =
"void main()               \n"
"{                         \n"
"  gl_FragColor = vec4(1.0, 1.0, 1.0, 1.0); \n" // Color
"}                         \n";



GLfloat landscapeVertices[] = {
   -0.8f,  0.73, 0.0f,  // Top Left
    0.8f,  0.73f, 0.0f,  // Top Right
    0.8f, -0.63f, 0.0f,  // Bottom Right
   -0.8f, -0.63f, 0.0f   // Bottom Left
};
GLfloat portraitVertices[] = {
   -0.8f,  1.0f, 0.0f,  // Top Left
    0.8f,  1.0f, 0.0f,  // Top Right
    0.8f, -0.67f, 0.0f,  // Bottom Right
   -0.8f, -0.67f, 0.0f   // Bottom Left
};
// Texture coordinates
GLfloat landscapeTexCoords[] = {
    0.0f, 0.07f,  // Bottom Left
    0.90f, 0.07f,  // Bottom Right
    0.90f, 1.0f,  // Top Right
    0.0f, 1.0f   // Top Left
};
// Texture coordinates
GLfloat portraitTexCoords[] = {
    0.0f, 0.0f,  // Bottom Left
    0.63f, 0.0f,  // Bottom Right
    0.63f, 0.2f,  // Top Right
    0.0f, 0.2f   // Top Left
};

// Constants for VNC protocol
const char* PROTOCOL_VERSION = "RFB 003.003\n"; // Client initialization message
const char FRAMEBUFFER_UPDATE_REQUEST[] = {3,0,0,0,0,0,255,255,255,255};
const char CLIENT_INIT[] = {1};
const char ZLIB_ENCODING[] = {2,0,0,2,0,0,0,6,0,0,0,0};

// SETUP SECTION
int windowWidth = 800;
int windowHeight = 480;

const char* VNC_SERVER_IP_ADDRESS = "192.168.1.198";
const int VNC_SERVER_PORT = 5900;

const char* EXLAP_SERVER_IP_ADDRESS = "127.0.0.1";
const int EXLAP_SERVER_PORT = 25010;


// WINDOWS SPECIFIC CODE SECTION
static void usleep(__int64 usec) {
    HANDLE timer;
    LARGE_INTEGER ft;

    // Convert microseconds to 100-nanosecond intervals
    ft.QuadPart = -(10 * usec);

    timer = CreateWaitableTimer(NULL, TRUE, NULL);
    SetWaitableTimer(timer, &ft, 0, NULL, NULL, 0);
    WaitForSingleObject(timer, INFINITE);
    CloseHandle(timer);
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

static void BuildSockAddr(SOCKADDR* const SockAddr, char const* const IPAddress, WORD const Port)
{
    SOCKADDR_IN* const SIn = (SOCKADDR_IN*)SockAddr;

    SIn->sin_family = AF_INET;
    inet_pton(AF_INET, IPAddress, &SIn->sin_addr);
    SIn->sin_port = htons(Port);
}

SOCKET MySocketOpen(int const Type, WORD const Port)
{

    int Protocol = (Type == SOCK_STREAM) ? IPPROTO_TCP : IPPROTO_UDP;

    SOCKET Socket = WSASocket(AF_INET, Type, Protocol, NULL, 0, 0);

    if (Socket != INVALID_SOCKET)
    {
        SOCKADDR SockAddr = { 0 };

        BuildSockAddr(&SockAddr, NULL, Port);

        if (bind(Socket, &SockAddr, sizeof(SockAddr)) != 0)
        {
            closesocket(Socket);
            Socket = INVALID_SOCKET;
        }
    }

    return Socket;
}



// Compile shader function
GLuint compileShader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    // Check for compilation errors
    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetShaderInfoLog(shader, 512, nullptr, infoLog);
        std::cerr << "Shader compilation error: " << infoLog << std::endl;
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}
// CODE FROM HERE IS THE SAME FOR WINDOWS OR QNX
void executeInitialCommands() {
    std::vector<std::pair<std::string, std::string>> commands = {
        {"on -f mmx /net/mmx/mnt/app/eso/bin/apps/pc i:1304:210 1", "Cannot enable AA sensors data"},
        {"/eso/bin/apps/dmdt sc 4 -9", "Set context of display 4 failed with error"},
        {"/eso/bin/apps/dmdt sc 0 71", "Switch context to 71 on display 0 failed with error"}
    };

    for (size_t i = 0; i < commands.size(); ++i) {
        std::string command = commands[i].first;
        std::string error_message = commands[i].second;
        std::cout << "Executing '" << command << "'" << std::endl;

        // Execute the command
        int ret = system(command.c_str());
        if (ret != 0) {
            std::cerr << error_message << ": " << ret << std::endl;
        }
    }
}

void executeFinalCommands() {
    std::vector<std::pair<std::string, std::string>> commands = {
        {"/eso/bin/apps/dmdt sc 4 70", "Set display 4 (VC) to display table 70 failed with error"}
    };

    for (size_t i = 0; i < commands.size(); ++i) {
        std::string command = commands[i].first;
        std::string error_message = commands[i].second;
        std::cout << "Executing '" << command << "'" << std::endl;

        // Execute the command
        int ret = system(command.c_str());
        if (ret != 0) {
            std::cerr << error_message << ": " << ret << std::endl;
        }
    }
}

std::string readPersistanceData(const std::string& position) {
    std::string command = "";
#ifdef _WIN32
    return ""; // not connected
#else
    command = "on -f mmx /net/mmx/mnt/app/eso/bin/apps/pc " + position;
#endif

    FILE* pipe = _popen(command.c_str(), "r");
    if (!pipe) {
        std::cerr << "Error: Failed to execute command." << std::endl;
        return "0";
    }

    char buffer[128];
    std::string result = "";
    while (!feof(pipe)) {
        if (fgets(buffer, 128, pipe) != NULL)
            result += buffer;
    }
    _pclose(pipe);

    std::cout << result;
    return result;
}

int16_t byteArrayToInt16(const char* byteArray) {
    return ((int16_t)(byteArray[0] & 0xFF) << 8) | (byteArray[1] & 0xFF);
}

int32_t byteArrayToInt32(const char* byteArray) {
    return ((int32_t)(byteArray[0] & 0xFF) << 24) | ((int32_t)(byteArray[1] & 0xFF) << 16) | ((int32_t)(byteArray[2] & 0xFF) << 8) | (byteArray[3] & 0xFF);
}


// Initialize OpenGL ES
void Init() {
    // Load and compile VNC shaders
    GLuint vertexShaderVncRender = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShaderVncRender, 1, &vertexShaderSource, NULL);
    glCompileShader(vertexShaderVncRender);

    // Check for compile errors
    GLint vertexShaderCompileStatus;
    glGetShaderiv(vertexShaderVncRender, GL_COMPILE_STATUS, &vertexShaderCompileStatus);
    if (vertexShaderCompileStatus != GL_TRUE) {
        char infoLog[512];
        glGetShaderInfoLog(vertexShaderVncRender, 512, NULL, infoLog);
        printf("Vertex shader compilation failed: %s\n", infoLog);
    }

    GLuint fragmentShaderVncRender = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShaderVncRender, 1, &fragmentShaderSource, NULL);
    glCompileShader(fragmentShaderVncRender);

    // Check for compile errors
    GLint fragmentShaderCompileStatus;
    glGetShaderiv(fragmentShaderVncRender, GL_COMPILE_STATUS, &fragmentShaderCompileStatus);
    if (fragmentShaderCompileStatus != GL_TRUE) {
        char infoLog[512];
        glGetShaderInfoLog(fragmentShaderVncRender, 512, NULL, infoLog);
        printf("Fragment shader compilation failed: %s\n", infoLog);
    }

    // Load and compile Text Render shaders
    GLuint vertexShaderTextRender = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShaderTextRender, 1, &vertexShaderSourceText, NULL);
    glCompileShader(vertexShaderTextRender);

    // Check for compile errors
    GLint vertexShaderTextRenderCompileStatus;
    glGetShaderiv(vertexShaderTextRender, GL_COMPILE_STATUS, &vertexShaderTextRenderCompileStatus);
    if (vertexShaderTextRenderCompileStatus != GL_TRUE) {
        char infoLog[512];
        glGetShaderInfoLog(vertexShaderTextRender, 512, NULL, infoLog);
        printf("Vertex shader compilation failed: %s\n", infoLog);
    }

    GLuint fragmentShaderTextRender = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShaderTextRender, 1, &fragmentShaderSourceText, NULL);
    glCompileShader(fragmentShaderTextRender);

    // Check for compile errors
    GLint fragmentShaderCompileStatusTextRender;
    glGetShaderiv(fragmentShaderTextRender, GL_COMPILE_STATUS, &fragmentShaderCompileStatusTextRender);
    if (fragmentShaderCompileStatus != GL_TRUE) {
        char infoLog[512];
        glGetShaderInfoLog(fragmentShaderTextRender, 512, NULL, infoLog);
        printf("Fragment shader compilation failed: %s\n", infoLog);
    }

    // Create program object
    programObject = glCreateProgram();
    
    glAttachShader(programObject, vertexShaderVncRender);
    glAttachShader(programObject, fragmentShaderVncRender);
    glLinkProgram(programObject);

    programObjectTextRender = glCreateProgram();
    glAttachShader(programObjectTextRender, vertexShaderTextRender);
    glAttachShader(programObjectTextRender, fragmentShaderTextRender);
    glLinkProgram(programObjectTextRender);

    // Check for linking errors
    GLint programLinkStatus;
    glGetProgramiv(programObject, GL_LINK_STATUS, &programLinkStatus);
    if (programLinkStatus != GL_TRUE) {
        char infoLog[512];
        glGetProgramInfoLog(programObject, 512, NULL, infoLog);
        printf("Program linking failed: %s\n", infoLog);
    }

    glGetProgramiv(programObjectTextRender, GL_LINK_STATUS, &programLinkStatus);
    if (programLinkStatus != GL_TRUE) {
        char infoLog[512];
        glGetProgramInfoLog(programObjectTextRender, 512, NULL, infoLog);
        printf("Program linking failed: %s\n", infoLog);
    }

    // Set clear color to black
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

}

char* parseFramebufferUpdate(SOCKET socket_fd, int* frameBufferWidth, int* frameBufferHeight, z_stream strm, int* finalHeight) 
{
    // Read message-type (1 byte) - not used, assuming it's always 0
    char messageType[1];
    if (!recv(socket_fd, messageType, 1, MSG_WAITALL)) {
        fprintf(stderr, "Error reading message type\n");
        return NULL;
    }

    // Read padding (1 byte) - unused
    char padding[1];
    if (!recv(socket_fd, padding, 1, MSG_WAITALL)) {
        fprintf(stderr, "Error reading padding\n");
        return NULL;
    }

    // Read number-of-rectangles (2 bytes)
    char numberOfRectangles[2];
    if (!recv(socket_fd, numberOfRectangles, 2, MSG_WAITALL)) {
        fprintf(stderr, "Error reading number of rectangles\n");
        return NULL;
    }

    // Calculate the total size of the message
    int totalLoadedSize = 0; // message-type + padding + number-of-rectangles
    char* finalFrameBuffer = (char*)malloc(1);
    int offset = 0;
    int ret = 0;
    // Now parse each rectangle
    for (int i = 0; i < byteArrayToInt16(numberOfRectangles); i++) {
        // Read rectangle header
        char xPosition[2];
        char yPosition[2];
        char width[2];
        char height[2];
        char encodingType[4]; // S32
        char compressedDataSize[4]; // S32

        if (!recv(socket_fd, xPosition, 2, MSG_WAITALL) ||
            !recv(socket_fd, yPosition, 2, MSG_WAITALL) ||
            !recv(socket_fd, width, 2, MSG_WAITALL) ||
            !recv(socket_fd, height, 2, MSG_WAITALL) ||
            !recv(socket_fd, encodingType, 4, MSG_WAITALL)) {
            fprintf(stderr, "Error reading rectangle header\n");
            return NULL;
        }

        *frameBufferWidth = byteArrayToInt16(width);
        *frameBufferHeight = byteArrayToInt16(height);
        *finalHeight = *finalHeight + *frameBufferHeight;
        if (encodingType[3] == '\x6') // ZLIB encoding
        {
            if (!recv(socket_fd, compressedDataSize, 4, MSG_WAITALL)) {
                fprintf(stderr, "Zlib compressedDataSize not found\n");
            }
            char* compressedData = (char*)malloc(byteArrayToInt32(compressedDataSize));
            int compresedDataReceivedSize = recv(socket_fd, compressedData, byteArrayToInt32(compressedDataSize), MSG_WAITALL);
            if (compresedDataReceivedSize < 0) {
                perror("error receiving framebuffer update rectangle");
                free(compressedData);
                return NULL;
            }

            // Allocate memory for decompressed data (assuming it's at most the same size as compressed)
            char* decompressedData = (char*)malloc(*frameBufferWidth * *frameBufferHeight * 4);
            if (!decompressedData) {
                perror("Error allocating memory for decompressed data");
                free(decompressedData);
                return NULL;
            }

            // Resize finalFrameBuffer to accommodate the appended data

            totalLoadedSize = totalLoadedSize + (*frameBufferWidth * *frameBufferHeight * 4);
            finalFrameBuffer = (char*)realloc(finalFrameBuffer, totalLoadedSize);

            // Decompress the data
            strm.avail_in = compresedDataReceivedSize;
            strm.next_in = (Bytef*)compressedData;
            strm.avail_out = *frameBufferWidth * *frameBufferHeight * 4; // Use the actual size of the decompressed data
            strm.next_out = (Bytef*)decompressedData;

            ret = inflate(&strm, Z_NO_FLUSH);

            if (ret < 0 && ret != Z_BUF_ERROR) {
                fprintf(stderr, "Error: Failed to decompress zlib data: %s\n", strm.msg);
                inflateEnd(&strm);
                free(decompressedData);
                free(compressedData);
                return NULL;
            }


            memcpy(finalFrameBuffer + offset, decompressedData, static_cast<size_t>(*frameBufferWidth) * *frameBufferHeight * 4);
            offset = offset + (*frameBufferWidth * *frameBufferHeight * 4);
            // Free memory allocated for framebufferUpdateRectangle
            free(compressedData);
            free(decompressedData);
        }
    }
    return finalFrameBuffer;
}

void print_string(float x, float y, const char* text, float r, float g, float b, float size) {
    char inputBuffer[2000] = { 0 }; // ~500 chars
    GLfloat triangleBuffer[2000] = { 0 };
    int number = stb_easy_font_print(0, 0, text, NULL, inputBuffer, sizeof(inputBuffer));

    // calculate movement inside viewport
    float ndcMovementX = (2.0f * x) / windowWidth;
    float ndcMovementY = (2.0f * y) / windowHeight;

    int triangleIndex = 0; // Index to keep track of the current position in the triangleBuffer
    // Convert each quad into two triangles and also apply size and offset to draw it to correct place
    for (int i = 0; i < sizeof(inputBuffer) / sizeof(GLfloat); i += 8) {
        // Triangle 1
        triangleBuffer[triangleIndex++] = *reinterpret_cast<GLfloat*>(&inputBuffer[i * sizeof(GLfloat)]) / size + ndcMovementX;
        triangleBuffer[triangleIndex++] = *reinterpret_cast<GLfloat*>(&inputBuffer[(i + 1) * sizeof(GLfloat)]) / size * -1 + ndcMovementY;
        triangleBuffer[triangleIndex++] = *reinterpret_cast<GLfloat*>(&inputBuffer[(i + 2) * sizeof(GLfloat)]) / size + +ndcMovementX;
        triangleBuffer[triangleIndex++] = *reinterpret_cast<GLfloat*>(&inputBuffer[(i + 3) * sizeof(GLfloat)]) / size * -1 + ndcMovementY;
        triangleBuffer[triangleIndex++] = *reinterpret_cast<GLfloat*>(&inputBuffer[(i + 4) * sizeof(GLfloat)]) / size + ndcMovementX;
        triangleBuffer[triangleIndex++] = *reinterpret_cast<GLfloat*>(&inputBuffer[(i + 5) * sizeof(GLfloat)]) / size * -1 + ndcMovementY;

        //// Triangle 2
        triangleBuffer[triangleIndex++] = *reinterpret_cast<GLfloat*>(&inputBuffer[i * sizeof(GLfloat)]) / size + ndcMovementX;
        triangleBuffer[triangleIndex++] = *reinterpret_cast<GLfloat*>(&inputBuffer[(i + 1) * sizeof(GLfloat)]) / size * -1 + ndcMovementY;
        triangleBuffer[triangleIndex++] = *reinterpret_cast<GLfloat*>(&inputBuffer[(i + 4) * sizeof(GLfloat)]) / size + ndcMovementX;
        triangleBuffer[triangleIndex++] = *reinterpret_cast<GLfloat*>(&inputBuffer[(i + 5) * sizeof(GLfloat)]) / size * -1 + ndcMovementY;
        triangleBuffer[triangleIndex++] = *reinterpret_cast<GLfloat*>(&inputBuffer[(i + 6) * sizeof(GLfloat)]) / size + ndcMovementX;
        triangleBuffer[triangleIndex++] = *reinterpret_cast<GLfloat*>(&inputBuffer[(i + 7) * sizeof(GLfloat)]) / size * -1 + ndcMovementY;

    }

    GLuint vbo;
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(triangleBuffer), triangleBuffer, GL_STATIC_DRAW);

    // Specify the layout of the vertex data
    GLint positionAttribute = glGetAttribLocation(programObject, "position");
    glEnableVertexAttribArray(positionAttribute);
    glVertexAttribPointer(positionAttribute, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    // glEnableVertexAttribArray(0);

     // Render the triangle
    glDrawArrays(GL_TRIANGLES, 0, triangleIndex);
    glDeleteBuffers(1, &vbo);
}

void print_string_center(float y, const char* text, float r, float g, float b, float size) {
    print_string(-stb_easy_font_width(text) * (size / 200), y, text, r, g, b, size);
}

// Helper function to parse a line for GLfloat arrays
void parseLineArray(char* line, const char* key, GLfloat* dest, int count) {
    if (strncmp(line, key, strlen(key)) == 0) {
        char* values = strchr(line, '=');
        if (values) {
            values++; // Skip '='
            for (int i = 0; i < count; i++) {
                dest[i] = strtof(values, &values); // Parse floats
            }
        }
    }
}

// Helper function to parse a line for integers
void parseLineInt(char* line, const char* key, int* dest) {
    if (strncmp(line, key, strlen(key)) == 0) {
        char* value = strchr(line, '=');
        if (value) {
            *dest = atoi(value + 1); // Parse integer
        }
    }
}

// Function to load the configuration file
void loadConfig(const char* filename) {
    FILE* file = NULL;
    errno_t err = fopen_s(&file, filename, "r");
    if (err != 0 || file == NULL) {
        printf("Config file not found or cannot be opened. Using defaults.\n");
        return;
    }

    char line[256];
    while (fgets(line, sizeof(line), file)) {
        parseLineArray(line, "landscapeVertices", landscapeVertices, 12);
        parseLineArray(line, "portraitVertices", portraitVertices, 12);
        parseLineArray(line, "landscapeTexCoords", landscapeTexCoords, 8);
        parseLineArray(line, "portraitTexCoords", portraitTexCoords, 8);
        parseLineInt(line, "windowWidth", &windowWidth);
        parseLineInt(line, "windowHeight", &windowHeight);
    }

    fclose(file);
}

// Function to print GLfloat arrays
void printArray(const char* label, GLfloat* array, int count, int elementsPerLine) {
    printf("%s:\n", label);
    for (int i = 0; i < count; i++) {
        printf("%f ", array[i]);
        if ((i + 1) % elementsPerLine == 0) printf("\n");
    }
    printf("\n");
}

// MAIN SECTION IS DIFFERENT ON WINDOWS
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    LPCWSTR className = L"OpenGL QNX render simulator";
    MSG msg;
    // Register class
    WNDCLASS wc = { 0 };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = className;
    wc.style = CS_OWNDC;
    RegisterClass(&wc);

    // Load config
    loadConfig("config.txt");

    // Print loaded or default values
    printArray("Landscape vertices", landscapeVertices, 12, 3);
    printArray("Portrait vertices", portraitVertices, 12, 3);
    printArray("Landscape texture coordinates", landscapeTexCoords, 8, 2);
    printArray("Portrait texture coordinates", portraitTexCoords, 8, 2);

    // Create window
    HWND hWnd = CreateWindowEx(0, className, L"OpenGL QNX render simulator", WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_OVERLAPPED,
        CW_USEDEFAULT, CW_USEDEFAULT, windowWidth, windowHeight, nullptr, nullptr, hInstance, nullptr);

    if (hWnd == NULL) {
        MessageBox(nullptr, L"Window Creation Failed!", L"Error!", MB_ICONEXCLAMATION | MB_OK);
        return 0;
    }

    // Initialize EGL
    eglDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLint majorVersion, minorVersion;
    eglInitialize(eglDisplay, &majorVersion, &minorVersion);

    // Setup EGL Configuration
    EGLint config_attribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 1,
        EGL_GREEN_SIZE, 1,
        EGL_BLUE_SIZE, 1,
        EGL_ALPHA_SIZE, 1,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_NONE
    };
    EGLConfig eglConfig;
    EGLint numConfigs;
    eglChooseConfig(eglDisplay, config_attribs, &eglConfig, 1, &numConfigs);

    // Create EGL Surface
    eglSurface = eglCreateWindowSurface(eglDisplay, eglConfig, hWnd, nullptr);
    eglBindAPI(EGL_OPENGL_ES_API);

    EGLint contextAttribs[] = {
    EGL_CONTEXT_CLIENT_VERSION, 2, // Request OpenGL ES 2.0
    EGL_NONE // Indicates the end of the attribute list
    };

    eglContext = eglCreateContext(eglDisplay, eglConfig, EGL_NO_CONTEXT, contextAttribs);
    eglMakeCurrent(eglDisplay, eglSurface, eglSurface, eglContext);

    // THIS SECTIONS IS THE SAME ON QNX AND WINDOWS
    // Initialize OpenGL ES
    Init();
    while (true)
    {
        WSADATA WSAData = { 0 };
        WSAStartup(0x202, &WSAData);
        SOCKET sockfd = MySocketOpen(SOCK_STREAM, 0);

        // Connect to VNC server
        sockaddr_in serverAddr;
        serverAddr.sin_family = AF_INET;
        inet_pton(AF_INET, VNC_SERVER_IP_ADDRESS, &serverAddr.sin_addr);
        serverAddr.sin_port = htons(VNC_SERVER_PORT); // VNC default port

        struct timeval timeout;
        timeout.tv_sec = 10; // 10 seconds timeout
        timeout.tv_usec = 0;

        // Set receive timeout
        if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout)) < 0) {
            perror("Set receive timeout failed");
            closesocket(sockfd);
            WSACleanup();
            continue;
        }

        // Set send timeout
        if (setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout)) < 0) {
            perror("Set send timeout failed");
            closesocket(sockfd);
            WSACleanup();
            continue;
        }

        if (connect(sockfd, reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr)) == SOCKET_ERROR) {
            std::cerr << "Connection failed" << std::endl;
            closesocket(sockfd);
            WSACleanup();
            usleep(200000);
            continue;
        }

        // Receive server initialization message
        char serverInitMsg[12];
        int bytesReceived = recv(sockfd, serverInitMsg, sizeof(serverInitMsg), MSG_WAITALL);

        if (bytesReceived == SOCKET_ERROR || bytesReceived == 0) {
            std::cerr << "Error receiving server initialization message" << std::endl;
            closesocket(sockfd);
            WSACleanup();
            continue;
        }

        // Send client protocol version message
        if (send(sockfd, PROTOCOL_VERSION, strlen(PROTOCOL_VERSION), 0) == SOCKET_ERROR) {
            std::cerr << "Error sending client initialization message" << std::endl;
            closesocket(sockfd);
            WSACleanup();
            continue;
        }

        // Security handshake
        char securityHandshake[4];
        int numOfTypes = recv(sockfd, securityHandshake, sizeof(securityHandshake), 0);
        printf("%s\n", securityHandshake);
        send(sockfd, "\x01", 1, 0); // ClientInit

        // Read framebuffer width and height
        char framebufferWidth[2];
        char framebufferHeight[2];

        if (!recv(sockfd, framebufferWidth, 2, 0) || !recv(sockfd, framebufferHeight, 2, 0)) {
            fprintf(stderr, "Error reading framebuffer dimensions\n");
            closesocket(sockfd);
            WSACleanup();
            continue;
        }

        // Read pixel format and name length
        char pixelFormat[16];
        char nameLength[4];

        if (!recv(sockfd, pixelFormat, sizeof(pixelFormat), MSG_WAITALL) ||
            !recv(sockfd, nameLength, sizeof(nameLength), MSG_WAITALL)) {
            fprintf(stderr, "Error reading pixel format or name length\n");
            closesocket(sockfd);
            WSACleanup();
            continue;
        }

        uint32_t nameLengthInt = (nameLength[0] << 24) | (nameLength[1] << 16) | (nameLength[2] << 8) | nameLength[3];

        // Read server name
        char name[32];
        if (!recv(sockfd, name, nameLengthInt, MSG_WAITALL)) {
            fprintf(stderr, "Error reading server name\n");
            closesocket(sockfd);
            WSACleanup();
            continue;
        }

        // Send encoding update requests
        if (send(sockfd, ZLIB_ENCODING, sizeof(ZLIB_ENCODING), 0) == SOCKET_ERROR ||
            send(sockfd, FRAMEBUFFER_UPDATE_REQUEST, sizeof(FRAMEBUFFER_UPDATE_REQUEST), 0) == SOCKET_ERROR) {
            std::cerr << "Error sending framebuffer update request" << std::endl;
            closesocket(sockfd);
            WSACleanup();
            continue;
        }
        int framebufferWidthInt = 0;
        int framebufferHeightInt = 0;
        int numberOfRectangles = 0;
        int finalHeight = 0;

        int frameCount = 0;
        int switchToMap = 0;
        double fps = 0.0;
        time_t startTime = time(NULL);
        GLuint textureID;
        glGenTextures(1, &textureID);
        glBindTexture(GL_TEXTURE_2D, textureID);

        // Set texture parameters
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        z_stream strm;
        strm.zalloc = Z_NULL;
        strm.zfree = Z_NULL;
        strm.opaque = Z_NULL;


        int ret = inflateInit(&strm);
        if (ret != Z_OK) {
            fprintf(stderr, "Error: Failed to initialize zlib decompression\n");
            closesocket(sockfd);
            WSACleanup();
            continue;
        }
        bool running = true;

        // Main loop
        while (true)
        {
            frameCount++;
            char* framebufferUpdate = parseFramebufferUpdate(sockfd, &framebufferWidthInt, &framebufferHeightInt, strm, &finalHeight);

            if (framebufferUpdate == NULL)
            {
                closesocket(sockfd);
                WSACleanup();
                free(framebufferUpdate);
                break;
            }

            //// Send update request
            if (send(sockfd, FRAMEBUFFER_UPDATE_REQUEST, sizeof(FRAMEBUFFER_UPDATE_REQUEST), 0) < 0) {
                std::cerr << "error sending framebuffer update request" << std::endl;
                closesocket(sockfd);
                free(framebufferUpdate);
                WSACleanup();
                break;
            }

            // Calculate elapsed time
            time_t currentTime = time(NULL);
            double elapsedTime = difftime(currentTime, startTime);

            // Calculate FPS every second
            if (elapsedTime >= 1.0) {
                // Calculate FPS
                fps = frameCount / elapsedTime;

                // Reset frame count and start time
                frameCount = 0;
                startTime = currentTime;
            }
            glClear(GL_COLOR_BUFFER_BIT); // clear all
            glUseProgram(programObject);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, framebufferWidthInt, finalHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, framebufferUpdate);

            // Set vertex positions
            GLint positionAttribute = glGetAttribLocation(programObject, "position");
            if (framebufferWidthInt > finalHeight)
               glVertexAttribPointer(positionAttribute, 3, GL_FLOAT, GL_FALSE, 0, landscapeVertices);
            else
                glVertexAttribPointer(positionAttribute, 3, GL_FLOAT, GL_FALSE, 0, portraitVertices);

            glEnableVertexAttribArray(positionAttribute);

            // Set texture coordinates
            GLint texCoordAttrib = glGetAttribLocation(programObject, "texCoord");
            if (framebufferWidthInt > finalHeight)
                glVertexAttribPointer(texCoordAttrib, 2, GL_FLOAT, GL_FALSE, 0, landscapeTexCoords);
            else
                glVertexAttribPointer(texCoordAttrib, 2, GL_FLOAT, GL_FALSE, 0, portraitTexCoords);

            glEnableVertexAttribArray(texCoordAttrib);
            finalHeight = 0;
            // Draw quad
            glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

            // Write text data to VC
            glUseProgram(programObjectTextRender);
            print_string(-333, 160, readPersistanceData("i:29229279:504").c_str(), 1, 1, 1, 150); // car speed

            eglSwapBuffers(eglDisplay, eglSurface);
            switchToMap++;
            if (switchToMap > 25)
            {
                switchToMap = 0;
                //execute_initial_commands(); DO NOT RUN ON WINDOWS THERE IS NO NEED
            }
            // Cleanup
            glDisableVertexAttribArray(0); // Disable the vertex attribute
            glBindBuffer(GL_ARRAY_BUFFER, 0); // Unbind the VBO
            free(framebufferUpdate);

            // Process Windows messages without blocking
            while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) {
                    running = false;
                    break;
                }

                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }

        }
        glDeleteTextures(1, &textureID);
    }
    // Cleanup
    eglSwapBuffers(eglDisplay, eglSurface);
    eglDestroyContext(eglDisplay, eglContext);
    eglDestroySurface(eglDisplay, eglSurface);
    eglTerminate(eglDisplay);
    //execute_final_commands(); DO NOT RUN ON WINDOWS THERE IS NO NEED

    return msg.wParam;
}
