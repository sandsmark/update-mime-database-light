#include <pugixml.hpp>

#include <iostream>
#include <vector>
#include <unordered_map>
#include <unordered_set>

#include <filesystem>
// it has obnoxiously long names thanks to the boost influence
namespace filesystem = std::filesystem;
using std::filesystem::exists;
using std::filesystem::last_write_time;
using std::filesystem::directory_iterator;
using std::filesystem::directory_entry;

struct MimeType {
    std::string media;
    std::string subType;
    pugi::xml_document doc;
};
struct Parser
{
    void loadSource(const filesystem::path &file);

    std::unordered_map<std::string, MimeType> mimeTypes;
    static const std::unordered_set<std::string> knownMediaTypes;
};

const std::unordered_set<std::string> Parser::knownMediaTypes = {
    "all",
    "uri",
    "print",
    "text",
    "application",
    "image",
    "audio",
    "inode",
    "video",
    "message",
    "model",
    "multipart",
    "x-content",
    "x-epoc",
    "x-scheme-handler",
    "font",
};

void Parser::loadSource(const filesystem::path &file)
{
    pugi::xml_document doc;
    if (!doc.load_file(file.c_str())) {
        std::cerr << "Failed to parse " << file << std::endl;
        return;
    }
    pugi::xml_node root = doc.child("mime-info");
    for (pugi::xml_node sourceNode = root.child("mime-type"); sourceNode; sourceNode = sourceNode.next_sibling("mime-type")) {
        const std::string type = sourceNode.attribute("type").value();
        if (type.empty()) {
            std::cerr << "Invalid node" << std::endl;
            continue;
        }
        std::string::size_type slashPos = type.find('/');
        if (slashPos == std::string::npos) {
            std::cerr << "Invalid type " << type << std::endl;
            continue;
        }

        MimeType mimeType;
        mimeType.media = type.substr(0, slashPos);
        mimeType.subType = type.substr(slashPos + 1);
        pugi::xml_node mimeNode = mimeType.doc.append_child("mime-type");
        mimeNode.append_attribute("type").set_value(type.c_str());
        mimeTypes[type] = std::move(mimeType);

        if (!knownMediaTypes.count(mimeType.media)) {
            std::cerr << "Unknown media type " << mimeType.media << std::endl;
        }

        for (pugi::xml_attribute attr = sourceNode.first_attribute(); attr; attr = attr.next_attribute()) {
            std::cout << " " << attr.name() << "=" << attr.value() << std::endl;;
        }

    }

}

static bool s_verbose = false;

void print_usage(const char *executable)
{
    std::cout << "Usage: " << executable << " [-hvVn] MIME-DIR" << std::endl;
}

int main(int argc, char *argv[])
{
    bool newerOnly = false;

    filesystem::path mimePath;
    for (int i=1; i<argc; i++) {
        if (argv[i][0] != '-') { // assume it is the directory
            mimePath = argv[i];
            continue;
        }

        switch(argv[i][1]) {
        case 'V':
            s_verbose = true;
            break;
        case 'n':
            newerOnly = true;
            break;
        case '?':
        case 'h':
        case 'v':
            print_usage(argv[0]);
            return 0;
        default:
            std::cout << "Invalid option " << argv[1] << std::endl;
            print_usage(argv[0]);
            return 1;
        }
    }

    if (!exists(mimePath)) {
        std::cerr << mimePath << " does not exist." << std::endl;
        print_usage(argv[0]);
        return 1;
    }

    filesystem::path packagesPath = mimePath/"packages";

    if (!exists(packagesPath)) {
        std::cerr << mimePath << " does not exist." << std::endl;
        print_usage(argv[0]);
        return 1;
    }

    const bool upToDate = last_write_time(mimePath/"version") >= last_write_time(packagesPath);
    if (upToDate && newerOnly) {
        if (s_verbose) {
            std::cout << "Up to date." << std::endl;
        }
        return 0;
    }

    if (s_verbose) {
        std::cout << "Updating..." << std::endl;
    }

    bool hasOverride = false;

    std::vector<filesystem::path> files;
    for (const filesystem::path &file : directory_iterator(packagesPath)) {
        if (file.extension() != ".xml") {
            if (s_verbose) {
                std::cout << "Unknown file " << file << std::endl;
            }
            continue;
        }

        if (file.filename() == "Override.xml") {
            hasOverride = true;
            continue;
        }
        files.push_back(file);
    }

    if (hasOverride) {
        files.push_back(packagesPath/"Override.xml");
    }

    Parser parser;
    for (const filesystem::path &file : files) {
        parser.loadSource(file);
    }

    return 0;
}
