const { keccak256 } = require('js-sha3');

/**
 * Convert storage slot number to checksum256 key (64 hex chars)
 */
function createStorageKey(slot) {
    return slot.toString(16).padStart(64, '0');
}

/**
 * Convert a value to uint256 representation (split into low/high 128 bits)
 */
function uint256(value) {
    const bn = BigInt(value);
    const mask128 = BigInt('0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF');
    const low = (bn & mask128).toString();
    const high = (bn >> BigInt(128)).toString();
    
    return {
        value_low: low,
        value_high: high
    };
}

/**
 * Add offset to a storage key (hex string)
 */
function addToKey(baseKey, offset) {
    const bn = BigInt('0x' + baseKey) + BigInt(offset);
    return bn.toString(16).padStart(64, '0');
}

/**
 * Convert Ethereum address to uint256 (padded to 32 bytes)
 */
function addressToUint256(address) {
    const cleanAddr = address.slice(2).toLowerCase();
    const paddedHex = cleanAddr.padStart(64, '0');
    
    const low = BigInt('0x' + paddedHex.slice(32));
    const high = BigInt('0x' + paddedHex.slice(0, 32));
    
    return {
        value_low: low.toString(),
        value_high: high.toString()
    };
}

/**
 * Encode string for Solidity short string storage (< 32 bytes inline)
 * Last byte contains length * 2
 */
function stringToStorageValue(str) {
    const hex = Buffer.from(str).toString('hex');
    const length = str.length;
    const paddedHex = hex.padEnd(62, '0') + (length * 2).toString(16).padStart(2, '0');
    
    const low = BigInt('0x' + paddedHex.slice(32));
    const high = BigInt('0x' + paddedHex.slice(0, 32));
    
    return {
        value_low: low.toString(),
        value_high: high.toString()
    };
}

/**
 * Calculate keccak256 hash of array storage slot for dynamic arrays
 */
function keccak256ArraySlot(slot) {
    const slotHex = slot.toString(16).padStart(64, '0');
    const hash = keccak256(Buffer.from(slotHex, 'hex'));
    return hash;
}

module.exports = {
    createStorageKey,
    uint256,
    addToKey,
    addressToUint256,
    stringToStorageValue,
    keccak256ArraySlot
};
