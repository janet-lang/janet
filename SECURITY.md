# Security Policy

## Supported Versions

Fixes, security or otherwise, are not backported to older Janet versions. All
fixes will be committed to master and released in the next Janet release.

## Reporting a Vulnerability

Please submit bugs to GitHub Issues. Like many interpreters and compilers,
nearly all bugs and miscompilations could potentially be exploitable in
some configuration or program. For example, we do not consider marshal
and unmarshal to be safe to expose over an untrusted network. Similarly,
Janet's interpreter sandbox escapes, bytecode verification failures, and
peg issues can be filed as normal bugs as well. We do not consider "eval" to
be a vulnerability.

If you have good reason, such as a particular application with an exploitable
vulnerability, or a particular vulnerability that is combined with a full
exploit of a working application and service, we will make an exception and
accept private disclosures. Disclosures will only be made public after a tested
and tagged release has been made public. Please submit to GitHub's provided
security reporting - https://github.com/janet-lang/janet/security

As with all contributions, please indicate any use by LLMs, fuzzers, static
analyzers, or any novel engineering tools, as it helps us track which
methodologies are productive and also combat spam.

## Responses

Responses will be best effort with no SLA.
