# cloud

This Cloudflare Worker accepts authenticated batches from the standalone ESP32 and serves a public status page backed by D1. The request contract is in `../SPEC.md` section 5.

## Local development

```bash
npm install
npx wrangler d1 migrations apply petrichor --local
npm test
npx wrangler dev
```

Local secrets belong in `.dev.vars`. Production uses `npx wrangler secret put CLOUD_SHARED_SECRET`.

## Deployment

```bash
npx wrangler d1 migrations apply petrichor --remote
npx wrangler deploy
```
