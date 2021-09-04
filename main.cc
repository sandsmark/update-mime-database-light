#include <pugixml.hpp>

#include <iostream>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <list>
#include <set>
#include <fstream>

#include <filesystem>
// it has obnoxiously long names thanks to the boost influence
namespace filesystem = std::filesystem;
using std::filesystem::exists;
using std::filesystem::last_write_time;
using std::filesystem::directory_iterator;
using std::filesystem::directory_entry;
using std::filesystem::create_directory;

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
        std::string type;
        std::string offset;
        std::string value;
        std::string mask;
        int lengthToCheck = -1;
    };

    int priority = 0;
    std::string mimetype;
    std::vector<Match> matches;

    Magic(const Magic&) = delete;

    Magic(const std::string &type, const pugi::xml_node &mimeNode);
};

struct Parser
{
    void loadSource(const filesystem::path &file);

    std::set<std::string> typeNames;

    std::unordered_map<std::string, MimeType> mimeTypes;
    static const std::unordered_set<std::string> knownMediaTypes;

    std::unordered_set<std::string> aliases;
    std::unordered_set<std::string> subclasses;
    std::unordered_set<std::string> genericIcons;
    std::unordered_set<std::string> icons;
    std::unordered_set<std::string> globs;
    std::unordered_set<std::string> xmlNamespaces;

    std::unordered_set<std::string> globs2;

    std::list<Magic> magics;

private:
    bool addMimetype(const std::string &type, const pugi::xml_node &sourceNode);

    void parseMagic(const std::string &type, const pugi::xml_node &mimeNode);
    void parseXMLNamespaces(const std::string &type, const pugi::xml_node &mimeNode);
    void parseFields(
            const std::string &fieldname,
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

Magic::Match::Match(const pugi::xml_node &matchNode)
{
    type = matchNode.attribute("type").value();
    value = matchNode.attribute("value").value();
    offset = matchNode.attribute("offset").value();
    mask = matchNode.attribute("mask").value();
}

void Parser::parseFields(
            const std::string &fieldname,
            const char *attribute,
            const std::string &mimetype,
            const pugi::xml_node &mimeNode,
            std::unordered_set<std::string> *set,
            const char separator
        )
{
    for (pugi::xml_node aliasNode = mimeNode.child(fieldname.c_str()); aliasNode; aliasNode = aliasNode.next_sibling(fieldname.c_str())) {
        const std::string type = aliasNode.attribute(attribute).value();
        if (type.empty()) {
            std::cerr << "Invalid alias node" << std::endl;
            continue;
        }
        const std::string line = type + separator + mimetype;
        set->insert(line);

        if (fieldname == "glob") {
            std::string weight = aliasNode.attribute("weight").value();
            if (weight.empty()) {
                weight = "50";
            }
            globs2.insert(weight + ":" + line);
        }
    }
}

Magic::Magic(const std::string &type, const pugi::xml_node &aliasNode)
{
    mimetype = type;
    try {
        const std::string pristr = aliasNode.attribute("priority").value();
        if (!pristr.empty()) {
            priority = std::stoi(pristr);
        }
    } catch (const std::exception&) { }

    for (pugi::xml_node matchNode = aliasNode.child("match"); matchNode; matchNode = matchNode.next_sibling("match")) {
        matches.emplace_back(matchNode);
    }
}

void Parser::parseMagic(const std::string &type, const pugi::xml_node &mimeNode)
{
    if (mimeNode.child("magic-deleteall")) {
        std::list<Magic>::iterator it = magics.begin();
        while (it != magics.end()) {
            if (it->mimetype == type) {
                it = magics.erase(it);
                continue;
            }
            it++;
        }
    }

    for (pugi::xml_node aliasNode = mimeNode.child("magic"); aliasNode; aliasNode = aliasNode.next_sibling("magic")) {
        magics.emplace_back(type, aliasNode);
    }
}

void Parser::parseXMLNamespaces(const std::string &type, const pugi::xml_node &mimeNode)
{
    for (pugi::xml_node aliasNode = mimeNode.child("root-XML"); aliasNode; aliasNode = aliasNode.next_sibling("root-XML")) {
        const std::string uri = aliasNode.attribute("namespaceURI").value();
        const std::string name = aliasNode.attribute("localName").value();
        if (uri.empty() || name.empty()) {
            std::cerr << "Invalid namespace node" << std::endl;
            continue;
        }
        xmlNamespaces.insert(uri + " " + name + " " + type);
    }
}

bool Parser::addMimetype(const std::string &type, const pugi::xml_node &sourceNode)
{
    std::string::size_type slashPos = type.find('/');
    if (slashPos == std::string::npos) {
        std::cerr << "Invalid type " << type << std::endl;
        return false;
    }

    MimeType &mimeType = mimeTypes.try_emplace(type).first->second;

    if (mimeType.media.empty()) {
        mimeType.media = type.substr(0, slashPos);
        mimeType.subType = type.substr(slashPos + 1);

        if (!knownMediaTypes.count(mimeType.media)) {
            std::cerr << "Unknown media type '" << mimeType.media << "'" << std::endl;
        }
    }

    pugi::xml_node mimeNode = mimeType.doc->child("mime-type");
    if (!mimeNode) {
        mimeNode = mimeType.doc->append_child("mime-type");
    }

    for (pugi::xml_attribute attr = sourceNode.first_attribute(); attr; attr = attr.next_attribute()) {
        mimeNode.append_copy(attr);
    }

    for (pugi::xml_node child = sourceNode.first_child(); child; child = child.next_sibling()) {
        const std::string name = child.name();
        if (name.empty()) {
            continue;
        }
        const char first = name[0];
        if (first == 'm' || first == 'g' || first == 'r') {
            if (name == "magic") {
                continue;
            }
            if (name == "glob") {
                continue;
            }
            if (name == "root-XML") {
                continue;
            }
        }
        mimeNode.append_copy(child);
    }

    if (s_verbose) {
        for (pugi::xml_attribute attr = sourceNode.first_attribute(); attr; attr = attr.next_attribute()) {
            std::cout << " " << attr.name() << "=" << attr.value() << std::endl;;
        }
        int foo = 0;
        if (foo++ < 10) {
            mimeType.doc->save(std::cout);
        }
    }
    return true;
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

        if (!addMimetype(type, sourceNode)) {
            continue;
        }

        parseFields("alias", "type", type, sourceNode, &aliases, ' ');
        parseFields("sub-class-of", "type", type, sourceNode, &subclasses, ' ');
        parseFields("generic-icon", "name", type, sourceNode, &genericIcons, ':');
        parseFields("icon", "name", type, sourceNode, &icons, ':');
        parseFields("glob", "pattern", type, sourceNode, &globs, ':');

        parseXMLNamespaces(type, sourceNode);
        parseMagic(type, sourceNode);
        typeNames.insert(type);
    }
}

void print_usage(const char *executable)
{
    std::cout << "Usage: " << executable << " [-hvVn] MIME-DIR" << std::endl;
}

template<typename Container>
static void writeFile(const std::string &filename, const Container &content, bool writeHeader = false)
{
    std::ofstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Failed to open " << filename << " for writing" << std::endl;
        return;
    }
    if (writeHeader) {
        file << "# This file was automatically generated by the\n"
                "# update-mime-database command. DO NOT EDIT!\n";
    }
    for (const std::string &line : content) {
        file << line << "\n";
    }
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

    filesystem::path versionPath = mimePath/"version";
    if (exists(versionPath)) {
        const bool upToDate = last_write_time(versionPath) >= last_write_time(packagesPath);
        if (upToDate && newerOnly) {
            if (s_verbose) {
                std::cout << "Up to date." << std::endl;
            }
            return 0;
        }
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

    writeFile(mimePath / "XMLnamespaces", parser.xmlNamespaces);
    writeFile(mimePath / "globs", parser.globs, true);
    writeFile(mimePath / "aliases", parser.aliases);
    writeFile(mimePath / "generic-icons", parser.genericIcons);
    writeFile(mimePath / "icons", parser.icons);
    writeFile(mimePath / "subclasses", parser.subclasses);
    writeFile(mimePath / "types", parser.typeNames);

    std::vector<std::string> globs2(parser.globs2.begin(), parser.globs2.end());
    std::sort(globs2.begin(), globs2.end(), std::greater<std::string>());
    writeFile(mimePath / "globs2", globs2, true);

    writeFile(mimePath / "version", std::vector<std::string>({"2.1"}));

    for (const std::pair<const std::string, MimeType> &p : parser.mimeTypes) {
        const MimeType &mimeType = p.second;
        create_directory(mimePath / mimeType.media);
        const filesystem::path path = mimePath / mimeType.media / (mimeType.subType + ".xml");
        mimeType.doc->save_file(path.c_str());
    }

    return 0;
}
