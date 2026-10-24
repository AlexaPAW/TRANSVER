#include "lexer.hpp"
#include "parser.hpp"
#include "semantic.hpp"
#include "ast.hpp"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void printUsage(const char* prog) {
    std::cout << "Usage:\n"
              << "  " << prog << " <file.funny> [--compact]\n"
              << "  " << prog << " --string <program> [--compact]\n"
              << "\nOutput: annotated AST as JSON on stdout; diagnostics on stderr.\n"
              << "Exit codes: 0 = no errors, 1 = lexical/parser/semantic errors, 2 = CLI/I/O error.\n";
}

std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open input file: " + path);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

void printDiagnostics(const std::vector<funny::Diagnostic>& diagnostics) {
    for (const auto& d : diagnostics) {
        std::cerr << d.phase << " error at " << d.pos.line << ':' << d.pos.column
                  << " (offset " << d.pos.offset << "): " << d.message << '\n';
    }
}

void writeOutput(const std::string& json, bool compact) {
    const char* testMode = std::getenv("FUNNY_TEST_MODE");
    if (testMode && std::string(testMode) == "1") return;
    std::filesystem::create_directories("output");
    std::ofstream out("output/annotated_ast.json", std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("cannot create output/annotated_ast.json");
    out << json;
    if (compact) out << '\n';
}
}

int main(int argc, char** argv) {
    try {
        if (argc < 2 || std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h") {
            printUsage(argv[0]);
            return argc < 2 ? 2 : 0;
        }
        bool compact = false;
        std::string source;
        const std::string first = argv[1];
        if (first == "--string") {
            if (argc < 3) { std::cerr << "--string requires program text\n"; return 2; }
            source = argv[2];
            for (int i = 3; i < argc; ++i) {
                if (std::string(argv[i]) == "--compact") compact = true;
                else { std::cerr << "unknown option: " << argv[i] << '\n'; return 2; }
            }
        } else {
            for (int i = 2; i < argc; ++i) {
                if (std::string(argv[i]) == "--compact") compact = true;
                else { std::cerr << "unknown option: " << argv[i] << '\n'; return 2; }
            }
            source = readFile(first);
        }

        funny::Lexer lexer(source);
        funny::LexResult lex = lexer.scan();
        if (!lex.diagnostics.empty()) { printDiagnostics(lex.diagnostics); return 1; }

        funny::Parser parser(lex.tokens);
        funny::ParseResult parsed = parser.parse();
        if (!parsed.diagnostics.empty()) { printDiagnostics(parsed.diagnostics); return 1; }

        funny::SemanticAnalyzer analyzer;
        funny::SemanticResult checked = analyzer.analyze(*parsed.program);
        const std::string json = funny::ast::toJson(*parsed.program, !compact);
        std::cout << json;
        if (compact) std::cout << '\n';
        writeOutput(json, compact);
        printDiagnostics(checked.diagnostics);
        return checked.diagnostics.empty() ? 0 : 1;
    } catch (const std::exception& e) {
        std::cerr << "fatal: " << e.what() << '\n';
        return 2;
    }
}
