import Foundation

struct AITranslateRequest: Codable {
    let query: String
}

struct AITranslateResponse: Codable {
    let originalQuery: String
    let translatedQuery: String
    let success: Bool
    let alreadySyntax: Bool
    let error: String?

    enum CodingKeys: String, CodingKey {
        case originalQuery = "original_query"
        case translatedQuery = "translated_query"
        case success
        case alreadySyntax = "already_syntax"
        case error
    }
}

struct AIStatusResponse: Codable {
    let status: String
    let aiAvailable: Bool
    let model: String

    enum CodingKeys: String, CodingKey {
        case status, model
        case aiAvailable = "ai_available"
    }
}

final class AIServiceClient {
    static let shared = AIServiceClient()

    private let baseURL: URL
    private let session: URLSession
    private let decoder: JSONDecoder

    init(port: Int = 19861) {
        self.baseURL = URL(string: "http://127.0.0.1:\(port)")!
        let config = URLSessionConfiguration.default
        config.timeoutIntervalForRequest = 12
        config.timeoutIntervalForResource = 15
        self.session = URLSession(configuration: config)
        self.decoder = JSONDecoder()
    }

    func translate(query: String) async throws -> AITranslateResponse {
        let url = baseURL.appendingPathComponent("/api/ai/translate")
        var request = URLRequest(url: url)
        request.httpMethod = "POST"
        request.setValue("application/json", forHTTPHeaderField: "Content-Type")
        request.httpBody = try JSONEncoder().encode(AITranslateRequest(query: query))

        let (data, response) = try await session.data(for: request)
        guard let httpResp = response as? HTTPURLResponse, httpResp.statusCode == 200 else {
            throw AIServiceError.badResponse
        }
        return try decoder.decode(AITranslateResponse.self, from: data)
    }

    func translateStream(query: String, onToken: @escaping (String) -> Void) async throws -> AITranslateResponse {
        let url = baseURL.appendingPathComponent("/api/ai/translate/stream")
        var request = URLRequest(url: url)
        request.httpMethod = "POST"
        request.setValue("application/json", forHTTPHeaderField: "Content-Type")
        request.httpBody = try JSONEncoder().encode(AITranslateRequest(query: query))

        let (stream, response) = try await session.bytes(for: request)
        guard let httpResp = response as? HTTPURLResponse, httpResp.statusCode == 200 else {
            throw AIServiceError.badResponse
        }

        var finalResult: AITranslateResponse?
        for try await line in stream.lines {
            if line.hasPrefix("data: ") {
                let json = String(line.dropFirst(6))
                if let data = json.data(using: .utf8) {
                    if let result = try? decoder.decode(AITranslateResponse.self, from: data) {
                        finalResult = result
                    } else if let token = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
                              let t = token["token"] as? String {
                        onToken(t)
                    }
                }
            }
        }

        guard let result = finalResult else { throw AIServiceError.badResponse }
        return result
    }

    func status() async throws -> AIStatusResponse {
        let url = baseURL.appendingPathComponent("/api/ai/status")
        let (data, response) = try await session.data(for: URLRequest(url: url))
        guard let httpResp = response as? HTTPURLResponse, httpResp.statusCode == 200 else {
            throw AIServiceError.badResponse
        }
        return try decoder.decode(AIStatusResponse.self, from: data)
    }

    func isAvailable() async -> Bool {
        let url = baseURL.appendingPathComponent("/api/ai/health")
        do {
            let (_, response) = try await session.data(for: URLRequest(url: url))
            return (response as? HTTPURLResponse)?.statusCode == 200
        } catch {
            return false
        }
    }
}

enum AIServiceError: LocalizedError {
    case badResponse
    case serviceUnavailable

    var errorDescription: String? {
        switch self {
        case .badResponse: return "AI service returned an invalid response"
        case .serviceUnavailable: return "AI service is not running"
        }
    }
}
