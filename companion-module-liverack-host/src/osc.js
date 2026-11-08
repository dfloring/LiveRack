// Minimal OSC 1.0 encoding and decoding (int32, float32, string), no dependencies

function pad4(n) {
	return (n + 3) & ~3
}

function encodeString(str) {
	const bytes = Buffer.from(str, 'utf8')
	const out = Buffer.alloc(pad4(bytes.length + 1))
	bytes.copy(out)
	return out
}

/**
 * @param {string} address
 * @param {Array<number|string|{type:'f',value:number}>} args  integers go as int32, {type:'f'} as float32
 */
export function encodeMessage(address, args = []) {
	let tags = ','
	const parts = []

	for (const a of args) {
		if (typeof a === 'string') {
			tags += 's'
			parts.push(encodeString(a))
		} else if (typeof a === 'object' && a !== null && a.type === 'f') {
			tags += 'f'
			const b = Buffer.alloc(4)
			b.writeFloatBE(a.value)
			parts.push(b)
		} else {
			tags += 'i'
			const b = Buffer.alloc(4)
			b.writeInt32BE(Math.round(Number(a)) | 0)
			parts.push(b)
		}
	}

	return Buffer.concat([encodeString(address), encodeString(tags), ...parts])
}

function readString(buf, pos) {
	let end = pos
	while (end < buf.length && buf[end] !== 0) end++
	return { value: buf.toString('utf8', pos, end), next: pad4(end + 1) }
}

/** Returns a list of { address, args } (bundles are flattened) */
export function decodePacket(buf, depth = 0) {
	if (buf.length < 4 || depth > 4) return []

	if (buf.length >= 16 && buf.toString('utf8', 0, 7) === '#bundle') {
		const out = []
		let pos = 16
		while (pos + 4 <= buf.length) {
			const len = buf.readInt32BE(pos)
			pos += 4
			if (len <= 0 || pos + len > buf.length) break
			out.push(...decodePacket(buf.subarray(pos, pos + len), depth + 1))
			pos += len
		}
		return out
	}

	const addr = readString(buf, 0)
	if (!addr.value.startsWith('/')) return []
	let pos = addr.next
	const args = []

	if (pos < buf.length && buf[pos] === 0x2c) {
		const tags = readString(buf, pos)
		pos = tags.next
		for (const t of tags.value.slice(1)) {
			if (t === 'i') {
				if (pos + 4 > buf.length) break
				args.push(buf.readInt32BE(pos))
				pos += 4
			} else if (t === 'f') {
				if (pos + 4 > buf.length) break
				args.push(buf.readFloatBE(pos))
				pos += 4
			} else if (t === 's') {
				const s = readString(buf, pos)
				args.push(s.value)
				pos = s.next
			} else if (t === 'T') args.push(1)
			else if (t === 'F') args.push(0)
			else break
		}
	}

	return [{ address: addr.value, args }]
}
