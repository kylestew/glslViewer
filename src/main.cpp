#include <sys/stat.h>

#ifndef PLATFORM_WINDOWS
#include <unistd.h>
#endif

#include <map>
#include <thread>
#include <mutex>
#include <atomic>
#include <iostream>
#include <fstream>

#include "gl/gl.h"
#include "window.h"
#include "sandbox.h"
#include "io/fs.h"
#include "io/osc.h"
#include "tools/text.h"

// GLOBAL VARIABLES
//============================================================================
//
std::atomic<bool> bRun(true);

void pal_sleep(uint64_t value)
{
#if defined(PLATFORM_WINDOWS)
    std::this_thread::sleep_for(std::chrono::microseconds(value));
#else
    usleep(value);
#endif 
}

//  List of FILES to watch and the variable to communicate that between process
WatchFileList files;
std::mutex filesMutex;
int fileChanged;

// Console elements
CommandList commands;
std::mutex  consoleMutex;
std::string outputFile      = "";
std::vector<std::string> cmds_arguments;    // Execute commands
bool        execute_exit    = false;

// Open Sound Control
Osc osc_listener;

std::string version = "1.6.0";
std::string name = "GlslViewer";
std::string header = name + " " + version + " by Patricio Gonzalez Vivo ( patriciogonzalezvivo.com )"; 

const unsigned int micro_wait = REST_SEC * 1000000;
bool fullFps = false;
bool timeOut = false;

// Here is where all the magic happens
Sandbox sandbox;

//================================================================= Threads
void runCmd(const std::string &_cmd, std::mutex &_mutex);
void fileWatcherThread();
void cinWatcherThread();

//================================================================= Functions
void onExit();
void printUsage(char * executableName) {
    std::cerr << "// " << header << std::endl;
    std::cerr << "// "<< std::endl;
    std::cerr << "// Swiss army knife of GLSL Shaders. Loads frag/vertex shaders, images and " << std::endl;
    std::cerr << "// geometries. Will reload automatically on changes. Support for multi  "<< std::endl;
    std::cerr << "// buffers, baground and postprocessing passes. Can render headlessly and "<< std::endl;
    std::cerr << "// into a file. Use POSIX STANDARD CONSOLE IN/OUT to comunicate (uniforms,"<< std::endl;
    std::cerr << "// camera position, scene description and  commands) to and with other "<< std::endl;
    std::cerr << "// programs. Compatible with Linux and MacOS, runs from command line with"<< std::endl;
    std::cerr << "// out X11 enviroment on RaspberryPi devices. "<< std::endl;
    std::cerr << "// "<< std::endl;
    std::cerr << "// For more information refer to repository:"<< std::endl;
    std::cerr << "// https://github.com/patriciogonzalezvivo/glslViewer"<< std::endl;
    std::cerr << "// "<< std::endl;
    std::cerr << "// Usage: " << executableName << " [Arguments]"<< std::endl;
    std::cerr << "// "<< std::endl;
    std::cerr << "// Arguments:" << std::endl;
    std::cerr << "// <shader>.frag [<shader>.vert] - load shaders" << std::endl;
    std::cerr << "// [<mesh>.(obj/.ply)] - load obj or ply file" << std::endl;
    std::cerr << "// [-vFlip] - all textures after will be flipped vertically" << std::endl;
    std::cerr << "// [<texture>.(png/jpg/hdr)] - load and assign texture to uniform order" << std::endl;
    std::cerr << "// [-<uniformName> <texture>.(png/jpg/hdr)] - add textures associated with different uniform sampler2D names" << std::endl;
    std::cerr << "// [-C <enviromental_map>.(png/jpg/hdr)] - load a environmental map as cubemap" << std::endl;
    std::cerr << "// [-c <enviromental_map>.(png/jpg/hdr)] - load a environmental map as cubemap but hided" << std::endl;
    std::cerr << "// [-sh <enviromental_map>.(png/jpg/hdr)] - load a environmental map as spherical harmonics array" << std::endl;
    std::cerr << "// [-x <pixels>] - set the X position of the billboard on the screen" << std::endl;
    std::cerr << "// [-y <pixels>] - set the Y position of the billboard on the screen" << std::endl;
    std::cerr << "// [-w <pixels>] - set the width of the window" << std::endl;
    std::cerr << "// [-h <pixels>] - set the height of the billboard" << std::endl;
    std::cerr << "// [-f|--fullscreen] - load the window in fullscreen" << std::endl;
    std::cerr << "// [-l|--life-coding] - live code mode, where the billboard is allways visible" << std::endl;
    std::cerr << "// [--headless] - headless rendering. Very useful for making images or benchmarking." << std::endl;
    std::cerr << "// [--nocursor] - hide cursor" << std::endl;
    std::cerr << "// [--fxaa] - set FXAA as postprocess filter" << std::endl;
    std::cerr << "// [-I<include_folder>] - add an include folder to default for #include files" << std::endl;
    std::cerr << "// [-D<define>] - add system #defines directly from the console argument" << std::endl;
    std::cerr << "// [-p <osc_port>] - open OSC listening port" << std::endl;
    std::cerr << "// [-o <file>.png] - save the viewport to an image file before exit" << std::endl;
    std::cerr << "// [-s/--sec <seconds>] - exit app after a specific amount of seconds" << std::endl;
    std::cerr << "// [-e/-E <command>] - execute command when start. Multiple -e flags can be chained" << std::endl;
    std::cerr << "// [-v/--version] - return glslViewer version" << std::endl;
    std::cerr << "// [--verbose] - turn verbose outputs on" << std::endl;
    std::cerr << "// [--help] - print help for one or all command" << std::endl;
}

void declareCommands() {
    commands.push_back(Command("help", [&](const std::string& _line){
        if (_line == "help") {
            std::cout << "// " << header << std::endl;
            std::cout << "// " << std::endl;
            for (unsigned int i = 0; i < commands.size(); i++) {
                std::cout << "// " << commands[i].description << std::endl;
            }
            return true;
        }
        else {
            std::vector<std::string> values = split(_line,',');
            if (values.size() == 2) {
                for (unsigned int i = 0; i < commands.size(); i++) {
                    if (commands[i].begins_with == values[1]) {
                        std::cout << "// " << commands[i].description << std::endl;
                    }
                }
            }
        }
        return false;
    },
    "help[,<command>]               print help for one or all command", false));

    commands.push_back(Command("version", [&](const std::string& _line){ 
        if (_line == "version") {
            std::cout << version << std::endl;
            return true;
        }
        return false;
    },
    "version                        return glslViewer version.", false));

    commands.push_back(Command("window_width", [&](const std::string& _line){ 
        if (_line == "window_width") {
            std::cout << getWindowWidth() << std::endl;
            return true;
        }
        return false;
    },
    "window_width                   return the width of the windows.", false));

    commands.push_back(Command("window_height", [&](const std::string& _line){ 
        if (_line == "window_height") {
            std::cout << getWindowHeight() << std::endl;
            return true;
        }
        return false;
    },
    "window_height                  return the height of the windows.", false));

    commands.push_back(Command("pixel_density", [&](const std::string& _line){ 
        if (_line == "pixel_density") {
            std::cout << getPixelDensity() << std::endl;
            return true;
        }
        return false;
    },
    "pixel_density                  return the pixel density.", false));

    commands.push_back(Command("screen_size", [&](const std::string& _line){ 
        if (_line == "screen_size") {
            glm::ivec2 screen_size = getScreenSize();
            std::cout << screen_size.x << ',' << screen_size.y << std::endl;
            return true;
        }
        return false;
    },
    "screen_size                    return the screen size.", false));

    commands.push_back(Command("viewport", [&](const std::string& _line){ 
        if (_line == "viewport") {
            glm::ivec4 viewport = getViewport();
            std::cout << viewport.x << ',' << viewport.y << ',' << viewport.z << ',' << viewport.w << std::endl;
            return true;
        }
        return false;
    },
    "viewport                       return the viewport size.", false));

    commands.push_back(Command("mouse", [&](const std::string& _line){ 
        if (_line == "mouse") {
            glm::vec2 pos = getMousePosition();
            std::cout << pos.x << "," << pos.y << std::endl;
            return true;
        }
        return false;
    },
    "mouse                          return the mouse position.", false));
    
    commands.push_back(Command("fps", [&](const std::string& _line){ 
        if (_line == "fps") {
            // Force the output in floats
            printf("%f\n", getFPS());
            return true;
        }
        return false;
    },
    "fps                            return u_fps, the number of frames per second.", false));

    commands.push_back(Command("delta", [&](const std::string& _line){ 
        if (_line == "delta") {
            // Force the output in floats
            printf("%f\n", getDelta());
            return true;
        }
        return false;
    },
    "delta                          return u_delta, the secs between frames.", false));

    commands.push_back(Command("time", [&](const std::string& _line){ 
        if (_line == "time") {
            // Force the output in floats
            printf("%f\n", getTime());
            return true;
        }
        return false;
    },
    "time                           return u_time, the elapsed time.", false));

    commands.push_back(Command("date", [&](const std::string& _line){ 
        if (_line == "date") {
            // Force the output in floats
            glm::vec4 date = getDate();
            std::cout << date.x << ',' << date.y << ',' << date.z << ',' << date.w << std::endl;
            return true;
        }
        return false;
    },
    "date                           return u_date as YYYY, M, D and Secs.", false));

    commands.push_back(Command("files", [&](const std::string& _line){ 
        if (_line == "files") {
            for (unsigned int i = 0; i < files.size(); i++) { 
                std::cout << std::setw(2) << i << "," << std::setw(12) << toString(files[i].type) << "," << files[i].path << std::endl;
            }
            return true;
        }
        return false;
    },
    "files                          return a list of files.", false));

    commands.push_back(Command("reload", [&](const std::string& _line){ 
        if (_line == "reload" || _line == "reload,all") {
            fullFps = true;
            for (unsigned int i = 0; i < files.size(); i++) {
                filesMutex.lock();
                fileChanged = i;
                filesMutex.unlock();
                pal_sleep(micro_wait * 10.0);
            }
            fullFps = false;
            return true;
        }
        else {
            std::vector<std::string> values = split(_line,',');
            if (values.size() == 2 && values[0] == "reload") {
                for (unsigned int i = 0; i < files.size(); i++) {
                    if (files[i].path == values[1]) {
                        filesMutex.lock();
                        fileChanged = i;
                        filesMutex.unlock();
                        return true;
                    } 
                }
            }
        }
        return false;
    },
    "reload[,<filename>]            reload one or all files", false));

    commands.push_back(Command("frag", [&](const std::string& _line){ 
        if (_line == "frag") {
            std::cout << sandbox.getSource(FRAGMENT) << std::endl;
            return true;
        }
        else {
            std::vector<std::string> values = split(_line,',');
            if (values.size() == 2) {
                if (isDigit(values[1])) {
                    // Line number
                    unsigned int lineNumber = toInt(values[1]) - 1;
                    std::vector<std::string> lines = split(sandbox.getSource(FRAGMENT),'\n', true);
                    if (lineNumber < lines.size()) {
                        std::cout << lineNumber + 1 << " " << lines[lineNumber] << std::endl; 
                    }
                    
                }
                else {
                    // Write shader into a file
                    std::ofstream out(values[1]);
                    out << sandbox.getSource(FRAGMENT);
                    out.close();
                }
                return true;
            }
            else if (values.size() > 2) {
                std::vector<std::string> lines = split(sandbox.getSource(FRAGMENT),'\n', true);
                for (unsigned int i = 1; i < values.size(); i++) {
                    unsigned int lineNumber = toInt(values[i]) - 1;
                    if (lineNumber < lines.size()) {
                        std::cout << lineNumber + 1 << " " << lines[lineNumber] << std::endl; 
                    }
                }
            }
        }
        return false;
    },
    "frag[,<filename>]              returns or save the fragment shader source code.", false));

    commands.push_back(Command("vert", [&](const std::string& _line){ 
        if (_line == "vert") {
            std::cout << sandbox.getSource(VERTEX) << std::endl;
            return true;
        }
        else {
            std::vector<std::string> values = split(_line,',');
            if (values.size() == 2) {
                if (isDigit(values[1])) {
                    // Line number
                    unsigned int lineNumber = toInt(values[1]) - 1;
                    std::vector<std::string> lines = split(sandbox.getSource(VERTEX),'\n', true);
                    if (lineNumber < lines.size()) {
                        std::cout << lineNumber + 1 << " " << lines[lineNumber] << std::endl; 
                    }
                    
                }
                else {
                    // Write shader into a file
                    std::ofstream out(values[1]);
                    out << sandbox.getSource(VERTEX);
                    out.close();
                }
                return true;
            }
            else if (values.size() > 2) {
                std::vector<std::string> lines = split(sandbox.getSource(VERTEX),'\n', true);
                for (unsigned int i = 1; i < values.size(); i++) {
                    unsigned int lineNumber = toInt(values[i]) - 1;
                    if (lineNumber < lines.size()) {
                        std::cout << lineNumber + 1 << " " << lines[lineNumber] << std::endl; 
                    }
                }
            }
        }
        return false;
    },
    "vert[,<filename>]              returns or save the vertex shader source code.", false));

    commands.push_back( Command("dependencies", [&](const std::string& _line){ 
        if (_line == "dependencies") {
            for (unsigned int i = 0; i < files.size(); i++) { 
                if (files[i].type == GLSL_DEPENDENCY) {
                    std::cout << files[i].path << std::endl;
                }   
            }
            return true;
        }
        else if (_line == "dependencies,frag") {
            sandbox.printDependencies(FRAGMENT);
            return true;
        }
        else if (_line == "dependencies,vert") {
            sandbox.printDependencies(VERTEX);
            return true;
        }
        return false;
    },
    "dependencies[,vert|frag]       returns all the dependencies of the vertex o fragment shader or both.", false));

    commands.push_back(Command("update", [&](const std::string& _line){ 
        if (_line == "update") {
            sandbox.flagChange();
        }
        return false;
    },
    "update                         force all uniforms to be updated", false));

    commands.push_back(Command("wait", [&](const std::string& _line){ 
        std::vector<std::string> values = split(_line,',');
        if (values.size() == 2) {
            pal_sleep( toFloat(values[1])*1000000 ); 
        }
        return false;
    },
    "wait,<seconds>                 wait for X <seconds> before excecuting another command."));

    commands.push_back(Command("fullFps", [&](const std::string& _line){
        if (_line == "fullFps") {
            std::string rta = fullFps ? "on" : "off";
            std::cout <<  rta << std::endl; 
            return true;
        }
        else {
            std::vector<std::string> values = split(_line,',');
            if (values.size() == 2) {
                consoleMutex.lock();
                fullFps = (values[1] == "on");
                consoleMutex.unlock();
            }
        }
        return false;
    },
    "fullFps[,on|off]               go to full FPS or not", false));

    commands.push_back(Command("cursor", [&](const std::string& _line){
        if (_line == "cursor") {
            std::string rta = sandbox.cursor ? "on" : "off";
            std::cout <<  rta << std::endl; 
            return true;
        }
        else {
            std::vector<std::string> values = split(_line,',');
            if (values.size() == 2) {
                consoleMutex.lock();
                sandbox.cursor = (values[1] == "on");
                consoleMutex.unlock();
            }
        }
        return false;
    },
    "cursor[,on|off]                show/hide cursor", false));

    commands.push_back(Command("screenshot", [&](const std::string& _line){ 
        if (_line == "screenshot" && outputFile != "") {
            sandbox.screenshotFile = outputFile;
            return true;
        }
        else {
            std::vector<std::string> values = split(_line,',');
            if (values.size() == 2) {
                consoleMutex.lock();
                sandbox.screenshotFile = values[1];
                consoleMutex.unlock();
                return true;
            }
        }
        return false;
    },
    "screenshot[,<filename>]        saves a screenshot to a filename.", false));

    commands.push_back(Command("sequence", [&](const std::string& _line){ 
        std::vector<std::string> values = split(_line,',');
        if (values.size() >= 3) {
            float from = toFloat(values[1]);
            float to = toFloat(values[2]);
            float fps = 24.0;

            if (values.size() == 4)
                fps = toFloat(values[3]);

            if (from >= to) {
                from = 0.0;
            }

            consoleMutex.lock();
            sandbox.record(from, to, fps);
            consoleMutex.unlock();

            std::cout << "// " << std::endl;

            int pct = 0;
            while (pct < 100) {
                // Delete previous line
                const std::string deleteLine = "\e[2K\r\e[1A";
                std::cout << deleteLine;

                // Check progres.
                consoleMutex.lock();
                pct = sandbox.getRecordedPorcentage();
                consoleMutex.unlock();
                
                std::cout << "// [ ";
                for (int i = 0; i < 50; i++) {
                    if (i < pct/2) {
                        std::cout << "#";
                    }
                    else {
                        std::cout << ".";
                    }
                }
                std::cout << " ] " << pct << "%" << std::endl;
                pal_sleep( micro_wait );
            }
            return true;
        }
        return false;
    },
    "sequence,<A_sec>,<B_sec>[,fps] saves a sequence of images from A to B second.", false));

    commands.push_back(Command("q", [&](const std::string& _line){ 
        if (_line == "q") {
            bRun.store(false);
            return true;
        }
        return false;
    },
    "q                              close glslViewer", false));

    commands.push_back(Command("quit", [&](const std::string& _line){ 
        if (_line == "quit") {
            // bRun.store(false);
            timeOut = true;
            return true;
        }
        return false;
    },
    "quit                           close glslViewer", false));

    commands.push_back(Command("exit", [&](const std::string& _line){ 
        if (_line == "exit") {
            // bRun.store(false);
            timeOut = true;
            return true;
        }
        return false;
    },
    "exit                           close glslViewer", false));
}

// Main program
//============================================================================
int main(int argc, char **argv){

    // Set the size
    glm::ivec4 windowPosAndSize = glm::ivec4(0);
    #if defined(DRIVER_VC) || defined(DRIVER_GBM) 
        // RASPBERRYPI default windows size (fullscreen)
        glm::ivec2 screen = getScreenSize();
        windowPosAndSize.z = screen.x;
        windowPosAndSize.w = screen.y;
    #else
        // OSX/LINUX default windows size
        windowPosAndSize.z = 500;
        windowPosAndSize.w = 500;
    #endif

    WindowStyle windowStyle = REGULAR;
    bool displayHelp = false;
    for (int i = 1; i < argc ; i++) {
        std::string argument = std::string(argv[i]);

        if (        std::string(argv[i]) == "-x" ) {
            i++;
            windowPosAndSize.x = toInt(std::string(argv[i]));
        }
        else if (   std::string(argv[i]) == "-y" ) {
            i++;
            windowPosAndSize.y = toInt(std::string(argv[i]));
        }
        else if (   std::string(argv[i]) == "-w" ||
                    std::string(argv[i]) == "--width" ) {
            i++;
            windowPosAndSize.z = toInt(std::string(argv[i]));
        }
        else if (   std::string(argv[i]) == "-h" ||
                    std::string(argv[i]) == "--height" ) {
            i++;
            windowPosAndSize.w = toInt(std::string(argv[i]));
        }
        else if (   std::string(argv[i]) == "--help" ) {
            displayHelp = true;
        }
        else if (   std::string(argv[i]) == "--headless" ) {
            windowStyle = HEADLESS;
        }
        else if (   std::string(argv[i]) == "-f" ||
                    std::string(argv[i]) == "--fullscreen" ) {
            windowStyle = FULLSCREEN;
        }
        else if (   std::string(argv[i]) == "-l" ||
                    std::string(argv[i]) == "--life-coding" ){
        #if defined(DRIVER_VC) || defined(DRIVER_GBM) 
            windowPosAndSize.x = windowPosAndSize.z - 500;
            windowPosAndSize.z = windowPosAndSize.w = 500;
        #else
            windowStyle = ALLWAYS_ON_TOP;
        #endif
        }
        
    }

    if (displayHelp) {
        printUsage( argv[0] );
        exit(0);
    }

    // Declare commands
    declareCommands();

    // Initialize openGL context
    initGL (windowPosAndSize, windowStyle);

    struct stat st;                         // for files to watch
    float       timeLimit       = -1.0f;    // Time limit
    int         textureCounter  = 0;        // Number of textures to load
    bool        vFlip           = true;     // Flip state

    //Load the the resources (textures)
    for (int i = 1; i < argc ; i++){
        std::string argument = std::string(argv[i]);

        if (    argument == "-x" || argument == "-y" ||
                argument == "-w" || argument == "--width" ||
                argument == "-h" || argument == "--height" ) {
            i++;
        }
        else if (   argument == "-l" ||
                    argument == "--headless") {
        }
        else if (   std::string(argv[i]) == "-f" ||
                    std::string(argv[i]) == "--fullscreen" ) {
        }
        else if (   argument == "--verbose" ) {
            sandbox.verbose = true;
        }
        else if ( argument == "--nocursor" ) {
            sandbox.cursor = false;
        }
        else if ( argument == "--fxaa" ) {
            sandbox.fxaa = true;
        }
        else if ( argument == "-s" || argument == "--sec" ) {
            i++;
            argument = std::string(argv[i]);
            timeLimit = toFloat(argument);
            std::cout << "// Will exit in " << timeLimit << " seconds." << std::endl;
        }
        else if ( argument == "-o" ) {
            i++;
            argument = std::string(argv[i]);
            if ( haveExt(argument, "png" )) {
                outputFile = argument;
                std::cout << "// Will save screenshot to " << outputFile  << " on exit." << std::endl;
            }
            else {
                std::cerr << "At the moment screenshots only support PNG formats" << std::endl;
            }
        }
        else if ( argument== "-p" || argument == "--port" ) {
            i++;
            osc_listener.start(toInt(std::string(argv[i])), runCmd);
        }
        else if ( argument == "-e" ) {
            i++;
            cmds_arguments.push_back(std::string(argv[i]));
        }
        else if ( argument == "-E" ) {
            i++;
            cmds_arguments.push_back(std::string(argv[i]));
            execute_exit = true;
        }
        else if (argument == "--fullFps" ) {
            fullFps = true;
        }
        else if ( sandbox.frag_index == -1 && (haveExt(argument,"frag") || haveExt(argument,"fs") ) ) {
            if ( stat(argument.c_str(), &st) != 0 ) {
                std::cout << "File " << argv[i] << " not founded. Creating a default fragment shader with that name"<< std::endl;

                std::ofstream out(argv[i]);
                out << default_scene_frag;
                out.close();
            }

            WatchFile file;
            file.type = FRAG_SHADER;
            file.path = argument;
            file.lastChange = st.st_mtime;
            files.push_back(file);

            sandbox.frag_index = files.size()-1;

        }
        else if ( sandbox.vert_index == -1 && ( haveExt(argument,"vert") || haveExt(argument,"vs") ) ) {
            if ( stat(argument.c_str(), &st) != 0 ) {
                std::cout << "File " << argv[i] << " not founded. Creating a default vertex shader with that name"<< std::endl;

                std::ofstream out(argv[i]);
                out << default_scene_vert;
                out.close();
            }

            WatchFile file;
            file.type = VERT_SHADER;
            file.path = argument;
            file.lastChange = st.st_mtime;
            files.push_back(file);

            sandbox.vert_index = files.size()-1;
        }
        else if ( sandbox.geom_index == -1 && ( haveExt(argument,"ply") || haveExt(argument,"PLY") ||
                                                haveExt(argument,"obj") || haveExt(argument,"OBJ") ||
                                                haveExt(argument,"glb") || haveExt(argument,"GLB") ||
                                                haveExt(argument,"gltf") || haveExt(argument,"GLTF") ) ) {
            if ( stat(argument.c_str(), &st) != 0) {
                std::cerr << "Error watching file " << argument << std::endl;
            }
            else {
                WatchFile file;
                file.type = GEOMETRY;
                file.path = argument;
                file.lastChange = st.st_mtime;
                files.push_back(file); 
                sandbox.geom_index = files.size()-1;
            }
        }
        else if (   argument == "-vFlip" ||
                    argument == "--vFlip" ) {
            vFlip = false;
        }
        else if (   haveExt(argument,"hdr") || haveExt(argument,"HDR") ||
                    haveExt(argument,"png") || haveExt(argument,"PNG") ||
                    haveExt(argument,"jpg") || haveExt(argument,"JPG") ||
                    haveExt(argument,"jpeg") || haveExt(argument,"JPEG")) {

            if ( sandbox.uniforms.addTexture("u_tex"+toString(textureCounter), argument, files, vFlip) )
                textureCounter++;
        }
        else if ( argument == "-c" || argument == "-sh" ) {
            i++;
            argument = std::string(argv[i]);
            sandbox.uniforms.setCubeMap(argument, files);
            sandbox.getScene().showCubebox = false;
        }
        else if ( argument == "-C" ) {
            i++;
            argument = std::string(argv[i]);
            sandbox.uniforms.setCubeMap(argument, files);
            sandbox.getScene().showCubebox = true;
        }
        else if ( argument.find("-D") == 0 ) {
            // Defines are added/remove once existing shaders
            // On multiple meshes files like OBJ, there can be multiple 
            // variations of meshes, that only get created after loading the sece
            // to work arround that defines are add post-loading as argument commands
            std::string define = std::string("define,") + argument.substr(2);
            cmds_arguments.push_back(define);
        }
        else if ( argument.find("-I") == 0 ) {
            std::string include = argument.substr(2);
            sandbox.include_folders.push_back(include);
        }
        else if (   argument == "-v" || 
                    argument == "--version") {
            std::cout << version << std::endl;
        }
        else if ( argument.find("-") == 0 ) {
            std::string parameterPair = argument.substr( argument.find_last_of('-') + 1 );
            i++;
            argument = std::string(argv[i]);
            sandbox.uniforms.addTexture(parameterPair, argument, files, vFlip);
        }
    }

    if (sandbox.verbose) {
       // query strings
        char *vendor = (char *)glGetString(GL_VENDOR);
        char *renderer = (char *)glGetString(GL_RENDERER);
        char *version = (char *)glGetString(GL_VERSION);
        char *glsl_version = (char *)glGetString(GL_SHADING_LANGUAGE_VERSION);
    
        printf("OpenGL ES\n");
        printf("  Vendor: %s\n", vendor);
        printf("  Renderer: %s\n", renderer);
        printf("  Version: %s\n", version);
        printf("  GLSL version: %s\n", glsl_version);

        // char *exts = (char *)glGetString(GL_EXTENSIONS);
        // printf("  Extensions: %s\n", exts);
        // printf("  Implementation limits:\n");

        int param;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &param);
        std::cout << "  GL_MAX_TEXTURE_SIZE = " << param << std::endl;

    }

    // If no shader
    if ( sandbox.frag_index == -1 && sandbox.vert_index == -1 && sandbox.geom_index == -1 ) {
        printUsage(argv[0]);
        onExit();
        exit(EXIT_FAILURE);
    }

    // Start watchers
    fileChanged = -1;
    std::thread fileWatcher( &fileWatcherThread );
    std::thread cinWatcher( &cinWatcherThread );

    // Start working on the GL context
    filesMutex.lock();
    sandbox.setup(files, commands);
    filesMutex.unlock();

    if (sandbox.verbose)
        std::cout << "Starting Render Loop" << std::endl; 
    
    // Render Loop
    while ( isGL() && bRun.load() ) {
        // Update
        updateGL();

        // Calculate if timeout required
        if ( timeLimit >= 0.0 && getTime() >= timeLimit ) {
            timeOut = true;
            sandbox.screenshotFile = outputFile;
        }

        glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );

        // Something change??
        if ( fileChanged != -1 ) {
            filesMutex.lock();
            sandbox.onFileChange( files, fileChanged );
            fileChanged = -1;
            filesMutex.unlock();
        }

        // If nothing in the scene change skip the frame and try to keep it at 60fps
        if (!timeOut && !fullFps && !sandbox.haveChange()) {
            pal_sleep( micro_wait );
            continue;
        }

        // Draw Scene
        sandbox.render();

        // Draw Cursor and 2D Debug elements
        sandbox.renderUI();

        // Finish drawing
        sandbox.renderDone();

        if ( timeOut && sandbox.screenshotFile == "" )
            bRun.store(false);
        else
            // Swap the buffers
            renderGL();
    }

    // If is terminated by the windows manager, turn bRun off so the fileWatcher can stop
    if ( !isGL() ) {
        bRun.store(false);
    }

    onExit();
    
    // Wait for watchers to end
    fileWatcher.join();


    // Force cinWatcher to finish (because is waiting for input)
#ifndef PLATFORM_WINDOWS
    pthread_t cinHandler = cinWatcher.native_handle();
    pthread_cancel( cinHandler );
#endif//
    exit(0);
}

// Events
//============================================================================
void onKeyPress (int _key) {
    if (_key == 'q' || _key == 'Q') {
        bRun = false;
        bRun.store(false);
    }
}

void onMouseMove(float _x, float _y) {
}

void onMouseClick(float _x, float _y, int _button) {
}

void onScroll(float _yoffset) {
    sandbox.onScroll(_yoffset);
}

void onMouseDrag(float _x, float _y, int _button) {
    sandbox.onMouseDrag(_x, _y, _button);
}

void onViewportResize(int _newWidth, int _newHeight) {
    sandbox.onViewportResize(_newWidth, _newHeight);
}

void onExit() {
    // clear screen
    glClear( GL_COLOR_BUFFER_BIT );

    // Delete the resources of Sandbox
    sandbox.clear();

    // close openGL instance
    closeGL();
}


//  Watching Thread
//============================================================================
void fileWatcherThread() {
    struct stat st;
    while ( bRun.load() ) {
        for ( uint32_t i = 0; i < files.size(); i++ ) {
            if ( fileChanged == -1 ) {
                stat( files[i].path.c_str(), &st );
                int date = st.st_mtime;
                if ( date != files[i].lastChange ) {
                    filesMutex.lock();
                    files[i].lastChange = date;
                    fileChanged = i;
                    filesMutex.unlock();
                }
            }
        }
        pal_sleep(500000);
    }
}

void runCmd(const std::string &_cmd, std::mutex &_mutex) {
    bool resolve = false;

    // Check if _cmd is present in the list of commands
    for (unsigned int i = 0; i < commands.size(); i++) {
        if (beginsWith(_cmd, commands[i].begins_with)) {

            // Do require mutex the thread?
            if (commands[i].mutex)
                _mutex.lock();

            // Execute de command
            resolve = commands[i].exec(_cmd);

            if (commands[i].mutex)
                _mutex.unlock();

            // If got resolved stop searching
            if (resolve) {
                break;
            }
        }
    }

    // If nothing match maybe the user is trying to define the content of a uniform
    if (!resolve) {
        _mutex.lock();
        sandbox.uniforms.parseLine(_cmd);
        _mutex.unlock();
    }
}

//  Command line Thread
//============================================================================
void cinWatcherThread() {
    while (!sandbox.isReady()) {
        pal_sleep( micro_wait );
    }

    // Argument commands to execute comming from -e or -E
    if (cmds_arguments.size() > 0) {
        for (unsigned int i = 0; i < cmds_arguments.size(); i++) {
            runCmd(cmds_arguments[i], consoleMutex);
        }
        cmds_arguments.clear();

        // If it's using -E exit after executing all commands
        if (execute_exit) {
            // bRun.store(false);
            timeOut = true;
        }
    }

    // Commands comming from the console IN
    std::string console_line;
    std::cout << "// > ";
    while (std::getline(std::cin, console_line)) {
        runCmd(console_line, consoleMutex);
        std::cout << "// > ";
    }
}
