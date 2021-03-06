#include "extensions/filters/http/cache/cacheability_utils.h"

#include "envoy/http/header_map.h"

#include "common/common/macros.h"
#include "common/common/utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

namespace {
const absl::flat_hash_set<absl::string_view>& cacheableStatusCodes() {
  // As defined by:
  // https://tools.ietf.org/html/rfc7231#section-6.1,
  // https://tools.ietf.org/html/rfc7538#section-3,
  // https://tools.ietf.org/html/rfc7725#section-3
  // TODO(yosrym93): the list of cacheable status codes should be configurable.
  CONSTRUCT_ON_FIRST_USE(absl::flat_hash_set<absl::string_view>, "200", "203", "204", "206", "300",
                         "301", "308", "404", "405", "410", "414", "451", "501");
}

const std::vector<const Http::LowerCaseString*>& conditionalHeaders() {
  // As defined by: https://httpwg.org/specs/rfc7232.html#preconditions.
  CONSTRUCT_ON_FIRST_USE(
      std::vector<const Http::LowerCaseString*>, &Http::CustomHeaders::get().IfMatch,
      &Http::CustomHeaders::get().IfNoneMatch, &Http::CustomHeaders::get().IfModifiedSince,
      &Http::CustomHeaders::get().IfUnmodifiedSince, &Http::CustomHeaders::get().IfRange);
}
} // namespace

Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::RequestHeaders>
    authorization_handle(Http::CustomHeaders::get().Authorization);
Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::ResponseHeaders>
    cache_control_handle(Http::CustomHeaders::get().CacheControl);

bool CacheabilityUtils::isCacheableRequest(const Http::RequestHeaderMap& headers) {
  const absl::string_view method = headers.getMethodValue();
  const absl::string_view forwarded_proto = headers.getForwardedProtoValue();
  const Http::HeaderValues& header_values = Http::Headers::get();

  // Check if the request contains any conditional headers.
  // For now, requests with conditional headers bypass the CacheFilter.
  // This behavior does not cause any incorrect results, but may reduce the cache effectiveness.
  // If needed to be handled properly refer to:
  // https://httpwg.org/specs/rfc7234.html#validation.received
  for (auto conditional_header : conditionalHeaders()) {
    if (headers.get(*conditional_header)) {
      return false;
    }
  }

  // TODO(toddmgreer): Also serve HEAD requests from cache.
  // Cache-related headers are checked in HttpCache::LookupRequest.
  return headers.Path() && headers.Host() && !headers.getInline(authorization_handle.handle()) &&
         (method == header_values.MethodValues.Get) &&
         (forwarded_proto == header_values.SchemeValues.Http ||
          forwarded_proto == header_values.SchemeValues.Https);
}

bool CacheabilityUtils::isCacheableResponse(const Http::ResponseHeaderMap& headers) {
  absl::string_view cache_control = headers.getInlineValue(cache_control_handle.handle());
  ResponseCacheControl response_cache_control(cache_control);

  // Only cache responses with explicit validation data, either:
  //    max-age or s-maxage cache-control directives with date header.
  //    expires header.
  const bool has_validation_data =
      (headers.Date() && response_cache_control.max_age_.has_value()) ||
      headers.get(Http::Headers::get().Expires);

  return !response_cache_control.no_store_ &&
         cacheableStatusCodes().contains((headers.getStatusValue())) && has_validation_data;
}

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
