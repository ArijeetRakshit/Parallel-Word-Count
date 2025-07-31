#include <iostream>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>
#include <algorithm> 
#include <filesystem> 

namespace fs = std::filesystem; 

bool cmp(const std::pair<std::string, int>& a, const std::pair<std::string, int>& b) {
    return a.second > b.second; 
}

int main() {
    std::cout << "Starting word count aggregation..." << std::endl;

    std::unordered_map<std::string, int> all_word_counts;
    int total_words_processed_across_consumers = 0;

    // Directory to scan for consumer output files (current directory)
    std::string output_dir = ".";
    std::string file_prefix = "consumer_output_";
    std::string file_suffix = ".txt";

    try {
        // Iterate through all entries in the current directory
        for (const auto& entry : fs::directory_iterator(output_dir)) {
            if (entry.is_regular_file()) {
                std::string filename = entry.path().filename().string();

                // Check if the filename matches our pattern "consumer_output_*.txt"
                if (filename.rfind(file_prefix, 0) == 0 &&
                    filename.length() > file_prefix.length() + file_suffix.length() &&
                    filename.substr(filename.length() - file_suffix.length()) == file_suffix) {

                    std::cout << "  Reading: " << filename << std::endl;
                    std::ifstream infile(entry.path());
                    if (!infile.is_open()) {
                        std::cerr << "Error: Could not open file " << filename << std::endl;
                        continue;
                    }

                    std::string line;
                    while (std::getline(infile, line)) {
                        size_t tab_pos = line.find('\t');
                        if (tab_pos != std::string::npos) {
                            std::string word = line.substr(0, tab_pos);
                            try {
                                int count = std::stoi(line.substr(tab_pos + 1));
                                all_word_counts[word] += count;
                                total_words_processed_across_consumers += count;
                            } catch (const std::invalid_argument& e) {
                                std::cerr << "Error: Invalid number format in line from " << filename << ": " << line << std::endl;
                            } catch (const std::out_of_range& e) {
                                std::cerr << "Error: Number out of range in line from " << filename << ": " << line << std::endl;
                            }
                        } else {
                            std::cerr << "Warning: Skipping malformed line in " << filename << ": " << line << std::endl;
                        }
                    }
                    infile.close();
                }
            }
        }
    } catch (const fs::filesystem_error& e) {
        std::cerr << "Filesystem error: " << e.what() << std::endl;
        return 1;
    }

    if (all_word_counts.empty()) {
        std::cout << "No word count data found from consumers. Please ensure consumers ran successfully." << std::endl;
        return 0;
    }

    // Convert map to vector for sorting
    std::vector<std::pair<std::string, int>> sorted_words;
    for (const auto& pair : all_word_counts) {
        sorted_words.push_back(pair);
    }

    // Sort by count in descending order
    std::sort(sorted_words.begin(), sorted_words.end(), cmp);

    // Write the final aggregated counts to the output file
    std::string final_output_filename = "aggregated_word_counts.txt";
    std::ofstream final_outfile(final_output_filename);
    if (!final_outfile.is_open()) {
        std::cerr << "Error: Could not open final output file " << final_output_filename << std::endl;
        return 1;
    }

    std::cout << "\nWriting truly aggregated results to '" << final_output_filename << "'" << std::endl;
    final_outfile << "--- Truly Aggregated Word Count Summary ---\n";
    final_outfile << "Total Unique Words: " << sorted_words.size() << "\n";
    final_outfile << "Total Words Processed (sum of all consumers): " << total_words_processed_across_consumers << "\n";
    final_outfile << "-------------------------------------------\n";
    for (const auto& pair : sorted_words) {
        final_outfile << pair.first << ": " << pair.second << "\n";
    }
    final_outfile.close();

    std::cout << "Aggregation complete! Results are in '" << final_output_filename << "'" << std::endl;

    return 0;
}