//Event: cms-bb-result-item
//Team: cms-bb-sport-events-team
//Opponents: cms-integration-sport-events-opponent-vs
//home/away: cms-bb-sport-events-homeaway
//Exact location: cms-bb-sport-events-address
//Map Link: cms-bb-sport-events-map
//Start time: cms-bb-sport-events-time
//Date wrapper: cms-bb-sport-events-date-wrapper
    //month: cms-bb-sport-events-month
    //Day: cms-bb-sport-events-fullday
    //Date/Num: cms-bb-sport-events-date
    //Year: cms-bb-sport-events-year

#include <algorithm>
#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
#include <vector>
#include <cstdlib>
#include <iomanip>

#include "api.h"

struct Event {
    std::string team;
    std::string opponent;
    std::string home_away;
    std::string location;
    std::string time_str;
    std::string month;
    std::string day_name;
    std::string date_num;
    std::string year;

    // Computed fields
    std::string title;
    std::string start_time;
    std::string end_time;
};

// 1. Download HTML using macOS/Linux built-in curl
// 1. Download HTML using MrScraper API bypass
std::string fetchHTML() {
    // The properly escaped curl command routing through the MrScraper API
// We use std::string() around the first part so C++ knows we are doing string math with the + operator
    std::string command = std::string("curl -s --location 'https://api.mrscraper.com?token=") + 
                          MRSCRAPER_API_KEY + 
                          "&geoCode=us&html=true&proxyCountry=us&url=https%3A%2F%2Fwww.brophyprep.org%2Fathletics' -H 'x-api-token: " + 
                          MRSCRAPER_API_KEY + 
                          "' > temp_page.html";
                          
    system(command.c_str());

    std::ifstream file("temp_page.html");
    std::stringstream buffer;
    buffer << file.rdbuf();
    file.close();

    // Clean up the temp file
    system("rm -f temp_page.html"); 
    return buffer.str();
}

// 2. Helper to extract text even if it's nested deep inside other tags
std::string extractTextForClass(const std::string& html, const std::string& className, size_t searchFrom = 0) {
    size_t classPos = html.find(className, searchFrom);
    if (classPos == std::string::npos) return "";

    // Find the end of the opening tag
    size_t startPos = html.find('>', classPos);
    if (startPos == std::string::npos) return "";
    startPos += 1;

    std::string content = "";
    bool inTag = false;
    int tagDepth = 1; // Start at 1 because we are inside the wrapper
    
    // Naive but effective way to handle nested wrappers
    for (size_t i = startPos; i < html.length(); ++i) {
        if (html[i] == '<') {
            if (html.substr(i, 2) == "</") {
                tagDepth--;
            } else if (html.substr(i, 2) != "<!" && html.substr(i, 2) != "<?") {
                tagDepth++;
            }
            inTag = true;
            if (tagDepth <= 0) break; // We've closed the main wrapper
        } else if (html[i] == '>') {
            inTag = false;
        } else if (!inTag) {
            content += html[i];
        }
    }
    
    // Trim surrounding whitespace/newlines
    size_t first = content.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = content.find_last_not_of(" \t\r\n");
    return content.substr(first, (last - first + 1));
}

// 3. Format Apple Timestamp using separate date components
std::string formatAppleTimestamp(std::string month, std::string day, std::string year, std::string timeStr) {
    const std::string months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", 
                                  "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    
    std::string monthNum = "01";
    for (int i = 0; i < 12; ++i) {
        if (month.find(months[i]) != std::string::npos) {
            std::stringstream ss;
            ss << std::setw(2) << std::setfill('0') << (i + 1);
            monthNum = ss.str();
            break;
        }
    }

    int dayInt = 0;
    
    // Scan through the messy string character by character
    for (size_t i = 0; i < day.length(); ++i) {
        // The moment we hit our first number...
        if (std::isdigit(day[i])) {
            // Read it, save it, and immediately stop searching
            dayInt = std::atoi(day.c_str() + i);
            break; 
        }
    }

    std::stringstream daySs;
    daySs << std::setw(2) << std::setfill('0') << (dayInt > 0 ? dayInt : 1);
    std::string dayNum = daySs.str();

    if (year.empty() || year.length() < 4) {
        year = "2026"; // Fallback just in case
    }

    int hours = 12;
    int minutes = 0;
    
    if (!timeStr.empty()) {
        // 1. Find the exact position of the first number
        size_t firstDigitPos = std::string::npos;
        for (size_t i = 0; i < timeStr.length(); ++i) {
            if (std::isdigit(timeStr[i])) {
                firstDigitPos = i;
                break;
            }
        }

        // 2. If we actually found a number (so it's not just "TBA")
        if (firstDigitPos != std::string::npos) {
            // Read the hours starting exactly from that first number
            hours = std::atoi(timeStr.c_str() + firstDigitPos);
            
            // Find the colon that comes AFTER the hours digits
            size_t colonPos = timeStr.find(':', firstDigitPos);
            if (colonPos != std::string::npos && colonPos + 1 < timeStr.length()) {
                // Read the minutes starting right after that specific colon
                minutes = std::atoi(timeStr.c_str() + colonPos + 1);
            }
        }

        bool isPM = (timeStr.find("PM") != std::string::npos || timeStr.find("pm") != std::string::npos);
        bool isAM = (timeStr.find("AM") != std::string::npos || timeStr.find("am") != std::string::npos);

        if (isPM && hours < 12) hours += 12;
        if (isAM && hours == 12) hours = 0;
    }

    std::stringstream timeFormatted;
    timeFormatted << std::setw(2) << std::setfill('0') << hours
                  << std::setw(2) << std::setfill('0') << minutes
                  << "00";

    return year + monthNum + dayNum + "T" + timeFormatted.str();
}

// 4. Calculate End Time
std::string calculateEndTime(const std::string& startTime, int durationHours = 2) {
    if (startTime.length() < 15) return startTime;

    int hour = std::atoi(startTime.substr(9, 2).c_str());
    hour = (hour + durationHours) % 24;

    std::stringstream newHour;
    newHour << std::setw(2) << std::setfill('0') << hour;

    std::string endTime = startTime;
    endTime.replace(9, 2, newHour.str());
    return endTime;
}

// Helper to decode HTML entities
std::string decodeHTMLEntities(std::string str) {
    size_t pos = 0;
    while ((pos = str.find("&amp;", pos)) != std::string::npos) str.replace(pos, 5, "&");
    pos = 0;
    while ((pos = str.find("&#039;", pos)) != std::string::npos) str.replace(pos, 6, "'");
    pos = 0;
    while ((pos = str.find("&quot;", pos)) != std::string::npos) str.replace(pos, 6, "\"");
    return str;
}

// 5. Parse event cards from HTML
std::vector<Event> parseEvents(const std::string& html) {
    std::vector<Event> events;
    size_t currentPos = 0;

    while ((currentPos = html.find("cms-bb-sport-events-result-data", currentPos)) != std::string::npos) {
        Event e;
        
        // Extract raw data
        e.team       = decodeHTMLEntities(extractTextForClass(html, "cms-bb-sport-events-team", currentPos));
        e.opponent   = decodeHTMLEntities(extractTextForClass(html, "cms-integration-sport-events-opponent-vs", currentPos));
        e.home_away  = decodeHTMLEntities(extractTextForClass(html, "cms-bb-sport-events-homeaway", currentPos));
        e.location   = decodeHTMLEntities(extractTextForClass(html, "cms-bb-sport-events-address", currentPos));
        e.time_str   = decodeHTMLEntities(extractTextForClass(html, "cms-bb-sport-events-time", currentPos));

        // Extract split date data
        e.month      = decodeHTMLEntities(extractTextForClass(html, "cms-bb-sport-events-month", currentPos));
        e.day_name   = decodeHTMLEntities(extractTextForClass(html, "cms-bb-sport-events-fullday", currentPos));
        e.date_num   = decodeHTMLEntities(extractTextForClass(html, "cms-bb-sport-events-date", currentPos));
        e.year       = decodeHTMLEntities(extractTextForClass(html, "cms-bb-sport-events-year", currentPos));

        // Assemble a summary string (e.g., "Varsity Baseball vs Eagles (Home)")
        e.title = e.team;
        
        e.start_time = formatAppleTimestamp(e.month, e.date_num, e.year, e.time_str);
        e.end_time   = calculateEndTime(e.start_time, 2);

        // 1. Turn all weird formatting (newlines, returns, tabs) into standard spaces
        std::replace(e.title.begin(), e.title.end(), '\n', ' ');
        std::replace(e.title.begin(), e.title.end(), '\r', ' ');
        std::replace(e.title.begin(), e.title.end(), '\t', ' ');

        // 2. Now that everything is on one line, crush the giant gaps
        size_t doubleSpacePos;
        while ((doubleSpacePos = e.title.find("  ")) != std::string::npos) {
            e.title.replace(doubleSpacePos, 2, " ");
        }

        // 3. Optional but helpful: strip a leading space if the string starts with one
        if (!e.title.empty() && e.title.front() == ' ') {
            e.title.erase(0, 1);
        }
        
        // 4. Optional but helpful: strip a trailing space if it ends with one
        if (!e.title.empty() && e.title.back() == ' ') {
            e.title.pop_back();
        }

        e.location = e.location.substr(e.location.find(':')+2);

        // Only add if we actually extracted a team
        if (!e.team.empty()) {
            events.push_back(e);
        }

        currentPos += 1; // move past the current class string
    }

    return events;
}

// 6. Generate the .ics file
void generateICS(const std::vector<Event>& events, const std::string& filename) {
    std::ofstream icsFile(filename);
    
    icsFile << "BEGIN:VCALENDAR\r\n";
    icsFile << "VERSION:2.0\r\n";
    icsFile << "PRODID:-//Brophy Prep Athletics C++ Scraper//EN\r\n";
    icsFile << "CALSCALE:GREGORIAN\r\n";

    for (size_t i = 0; i < events.size(); ++i) {
        icsFile << "BEGIN:VEVENT\r\n";
        icsFile << "UID:brophy-prep-event-" << i << "@mycustomdomain.com\r\n";
        
        icsFile << "DTSTART:" << events[i].start_time << "\r\n";
        icsFile << "DTEND:" << events[i].end_time << "\r\n";
        
        icsFile << "SUMMARY:" << events[i].title << "\r\n";
        icsFile << "LOCATION:" << events[i].location << "\r\n";
        
        // Add all extra details into the description
        icsFile << "DESCRIPTION:" << events[i].home_away << "\r\n";
        icsFile << "\r\n";
        
        icsFile << "END:VEVENT\r\n";
    }

    icsFile << "END:VCALENDAR\r\n";
    icsFile.close();
}

int main() {
    std::vector<std::string> months = {
        "january", "february", "march", "april", "may", "june",
        "july", "august", "september", "october", "november", "december"
    };


    std::string targetURL = "https://www.brophyprep.org/athletics";

    std::cout << "Scraping " << targetURL << "..." << std::endl;
        
    std::string html = fetchHTML();
    std::vector<Event> allEvents = parseEvents(html);
        
    std::cout << "\nTotal events found: " << allEvents.size() << std::endl;

    generateICS(allEvents, "calendar.ics");
    std::cout << "Successfully saved all events to calendar.ics!" << std::endl;
    return 0;
}
