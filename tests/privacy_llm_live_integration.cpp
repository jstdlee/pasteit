// Live check of the privacy + page-summary chain against a real
// OpenAI-compatible server: the model must only ever see placeholders and
// the answer must come back with the real values.
//   GENERAL_LLM_URL (default http://127.0.0.1:8010), GENERAL_LLM_MODEL
//   (default dgemma), GENERAL_LLM_API_KEY or API_KEY; PAGE_URL optional.
#include "ai/openai_compatible_client.hpp"
#include "net/http_client.hpp"
#include "net/page_text.hpp"
#include "privacy/anonymizer.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {
std::string env_or(const char* name, const char* fallback) {
    const char* value = std::getenv(name);
    return value != nullptr && *value != '\0' ? value : fallback;
}
}  // namespace

int main() {
    const auto endpoint = env_or("GENERAL_LLM_URL", "http://127.0.0.1:8010");
    const auto model = env_or("GENERAL_LLM_MODEL", "dgemma");
    const auto key = env_or("GENERAL_LLM_API_KEY", env_or("API_KEY", "").c_str());
    std::cout << "endpoint=" << endpoint << " model=" << model << "\n";

    std::string document =
        "Quarterly update. Revenue grew 12% to $4.2M in Q3. Contact Sarah Johnson at sarah.j@acme.com or "
        "+65 9123 4567. The VPN gateway is 10.20.30.40.";
    if (const char* page_url = std::getenv("PAGE_URL")) {
        const auto response = pastit::make_default_http_transport()->get_json(
            {.url = page_url, .body = {}, .headers = {}, .timeout = std::chrono::milliseconds{5000},
             .follow_redirects = true, .max_body_bytes = 2 * 1024 * 1024});
        if (!response.transport_error.empty() || response.status != 200) {
            std::cerr << "page fetch failed: " << response.transport_error << " HTTP " << response.status << "\n";
            return 1;
        }
        const auto page = pastit::extract_page_text(response.body, response.content_type);
        document = "Title: " + page.title + "\n\n" + page.text;
        std::cout << "page text bytes=" << document.size() << "\n";
    }

    pastit::PlaceholderVault vault;
    const auto anonymized = pastit::anonymize_text(document, {}, &vault);
    std::cout << "sent:\n" << anonymized.text << "\n";
    for (const auto& finding : anonymized.findings) {
        if (anonymized.text.find(finding.text) != std::string::npos) {
            std::cerr << "LEAK: " << finding.text << " is still in the request\n";
            return 1;
        }
    }
    pastit::OpenAiCompatibleClient client;
    const auto result = client.generate({.request_id = "live-privacy", .endpoint = endpoint, .api_key = key, .model_id = model,
                                         .system_message = "Summarize for a busy reader in two short sentences. Keep every "
                                                           "placeholder like [NAME_1] exactly as written.",
                                         .user_message = anonymized.text, .temperature = 0.2,
                                         .timeout = std::chrono::milliseconds{60000}});
    if (!result.ok) {
        std::cerr << "LLM failed: " << result.error << "\n";
        return 1;
    }
    std::cout << "model answer:\n" << result.content << "\nrestored:\n" << vault.restore(result.content) << "\n";
    return 0;
}
