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
    std::shared_ptr<pugi::xml_document> doc;

    MimeType() {
        doc = std::make_shared<pugi::xml_document>();
    }
};

struct Magic {
    struct Match {
        Match(const pugi::xml_node &mimeNode);
        Match(const Match &) = default;
        Match() = default;

        int indent = -1;
        int startOffset = 0;
        std::vector<uint8_t> value;
        std::vector<uint8_t> mask;
        int lengthToCheck = -1;
    };

    int priority = 0;
    std::string mimetype;
    std::vector<Match> matches;
};
struct Parser
{
    void loadSource(const filesystem::path &file);

    std::unordered_map<std::string, MimeType> mimeTypes;
    static const std::unordered_set<std::string> knownMediaTypes;

    std::unordered_set<std::string> aliases;
    std::unordered_set<std::string> subclasses;
    std::unordered_set<std::string> genericIcons;
    std::unordered_set<std::string> icons;
    std::unordered_set<std::string> globs;
    std::unordered_set<std::string> xmlNamespaces;

private:
    void addMimetype(const std::string &type, MimeType &&mimeType, const pugi::xml_node &sourceNode);

    void parseAliases(const std::string &mimetype, const pugi::xml_node &node);
    void parseMagic(const pugi::xml_node &mimeNode);
    void parseXMLNamespaces(const pugi::xml_node &mimeNode);
    void parseFields(
            const char *fieldname,
            const char *attribute,
            const std::string &mimetype,
            const pugi::xml_node &mimeNode,
            std::unordered_set<std::string> *set,
            const char separator
            );
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

static bool s_verbose = false;

Magic::Match::Match(const pugi::xml_node &mimeNode)
{
}

void Parser::parseFields(
            const char *fieldname,
            const char *attribute,
            const std::string &mimetype,
            const pugi::xml_node &mimeNode,
            std::unordered_set<std::string> *set,
            const char separator
        )
{
    for (pugi::xml_node aliasNode = mimeNode.child(fieldname); aliasNode; aliasNode = aliasNode.next_sibling(fieldname)) {
        const std::string type = aliasNode.attribute(attribute).value();
        if (type.empty()) {
            std::cerr << "Invalid alias node" << std::endl;
            continue;
        }
        set->insert(type + separator + mimetype);
    }
}

void Parser::parseMagic(const pugi::xml_node &mimeNode)
{
}

void Parser::parseXMLNamespaces(const pugi::xml_node &mimeNode)
{
}

void Parser::addMimetype(const std::string &type, MimeType &&mimeType, const pugi::xml_node &sourceNode)
{
    std::unordered_map<std::string, MimeType>::iterator it =
        mimeTypes.try_emplace(type, std::move(mimeType)).first;

    pugi::xml_node mimeNode = it->second.doc->child("mime-type");
    if (!mimeNode) {
        mimeNode = it->second.doc->append_child("mime-type");
    }

    for (pugi::xml_attribute attr = sourceNode.first_attribute(); attr; attr = attr.next_attribute()) {
        mimeNode.append_copy(attr);
    }

    for (pugi::xml_node child = sourceNode.first_child(); child; child = child.next_sibling()) {
        const std::string name = child.name();
        if (name == "magic") {
            continue;
        }
        if (name == "glob") {
            continue;
        }
        if (name == "root-XML") {
            continue;
        }
        mimeNode.append_copy(child);
    }

    if (s_verbose) {
        for (pugi::xml_attribute attr = sourceNode.first_attribute(); attr; attr = attr.next_attribute()) {
            std::cout << " " << attr.name() << "=" << attr.value() << std::endl;;
        }
        int foo = 0;
        if (foo++ < 10) {
            it->second.doc->save(std::cout);
        }
    }
}

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

        parseFields("alias", "type", type, sourceNode, &aliases, ' ');
        parseFields("sub-class-of", "type", type, sourceNode, &subclasses, ' ');
        parseFields("generic-icon", "name", type, sourceNode, &genericIcons, ':');
        parseFields("icon", "name", type, sourceNode, &icons, ':');
        parseFields("glob", "pattern", type, sourceNode, &globs, ':');

        parseMagic(sourceNode);

        MimeType mimeType;
        mimeType.media = type.substr(0, slashPos);
        mimeType.subType = type.substr(slashPos + 1);

        if (!knownMediaTypes.count(mimeType.media)) {
            std::cerr << "Unknown media type '" << mimeType.media << "'" << std::endl;
        }

        addMimetype(type, std::move(mimeType), sourceNode);
    }
}

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
