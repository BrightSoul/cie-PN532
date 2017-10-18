/**************************************************************************/
/*!
    @file     cie_BerReader.cpp
    @author   Developers Italia
    @license  BSD (see License)

	
	Reads and parses a BER encoded binary content

	@section  HISTORY

	v1.0  - Reading of fragments and binary values
*/
/**************************************************************************/
#include "cie_BerReader.h"


#define PN532DEBUGPRINT Serial

//Please refer to https://en.wikipedia.org/wiki/X.690#Identifier_octets

cie_BerReader::cie_BerReader (cie_PN532* cie) :
_cie(cie),
_currentOffset(0)
{   
}


/**************************************************************************/
/*!
  @brief Reads the triples hierarchy in the BER encoded file
  
  @param filePath a structure indicating the parent Dedicated File (either ROOT_MF or CIE_DF), the selection mode (either SELECT_BY_EFID or SELECT_BY_SFI) and the file identifier (either a sfi or an efid)	
  @param rootTriple The pointer to the root triple which will contain all of the others as its children
  @param length The pointer to the outer length (tag octets + length octets + content length)
  @param maxDepth How deep down in the hierarchy we should look for triples

  @returns  A value indicating whether the operation succeeded or not
*/
/**************************************************************************/
bool cie_BerReader::readTriples(const cie_EFPath filePath, cie_BerTriple*& rootTriple, word* length, const byte maxDepth) {
  resetCursor();
  if (maxDepth < 1) {
    rootTriple = nullptr;
    PN532DEBUGPRINT.println(F("Warning: you choose a maxDepth of 0 which won't read any triple"));
    return true;
  }

  byte currentDepth = 1;
  cie_BerTriple** tripleStack = new cie_BerTriple*[maxDepth];
  do {

    word tripleLength;
    if (!readTriple(filePath, tripleStack[currentDepth-1], &tripleLength)) {
      return false;
    }

    if (tripleStack[currentDepth-1]->contentLength > BER_READER_MAX_LENGTH) {
      PN532DEBUGPRINT.print(F("Sorry, we don't support content lengths greater than "));
      PN532DEBUGPRINT.println(BER_READER_MAX_LENGTH);
      return false;
    }

    if (tripleStack[currentDepth-1]->contentOffset > BER_READER_MAX_OFFSET) {
      PN532DEBUGPRINT.print(F("Sorry, we don't support content offsets greater than "));
      PN532DEBUGPRINT.println(BER_READER_MAX_OFFSET);
      return false;
    }

    word contentLength = tripleStack[currentDepth-1]->contentLength;
    if (currentDepth == 1) {
      *length = tripleLength;
      rootTriple = tripleStack[0];
    }

    //check if we've finished reading a parent (or granparent) triple and go up levels accordingly
    if (currentDepth > 1) {
      //add this to its parent
      byte childrenCount = tripleStack[currentDepth-2]->childrenCount + 1;
      tripleStack[currentDepth-2]->childrenCount = childrenCount;
      cie_BerTriple** children = new cie_BerTriple*[childrenCount];
      for (byte i = 0; i < childrenCount-1; i++) {
        children[i] = tripleStack[currentDepth-2]->children[i];
      }
      children[childrenCount-1] = tripleStack[currentDepth-1];
      delete [] tripleStack[currentDepth-2]->children;
      tripleStack[currentDepth-2]->children = children;

      //we can go up only if we're not at the root level
      for (byte i = currentDepth-1; i>0; i--) {

        bool isConstructed = tripleStack[i-1]->encoding == 0x01;
        bool isLastInParent = _currentOffset + contentLength >= tripleStack[i-1]->contentOffset + tripleStack[i-1]->contentLength;
        bool canGoDown = currentDepth + 1 <= maxDepth;
        bool stillToRead = _currentOffset < tripleStack[i-1]->contentOffset + tripleStack[i-1]->contentLength;

        if (isLastInParent && !(isConstructed && canGoDown && stillToRead)) {
          currentDepth-=1;        
        } else {
          break;
        }
      }

      
    }

    

    //Let's see if we should go down one level
    bool isConstructed = tripleStack[currentDepth-1]->encoding == 0x01;
    bool isAtBeginning = _currentOffset == tripleStack[currentDepth-1]->contentOffset;
    bool hasNonZeroLength = tripleStack[currentDepth-1]->contentLength > 0;
    bool canGoDown = currentDepth + 1 <= maxDepth;
    if (isConstructed && isAtBeginning && hasNonZeroLength && canGoDown) {
      currentDepth += 1;
    } else {
      _currentOffset += contentLength;
    }

  } while(_currentOffset < *length);
  return true;
}


/**************************************************************************/
/*!
  @brief Extracts informations from a single triple
  
  @param filePath a structure indicating the parent Dedicated File (either ROOT_MF or CIE_DF), the selection mode (either SELECT_BY_EFID or SELECT_BY_SFI) and the file identifier (either a sfi or an efid)	
  @param triple The pointer to the triple object
  @param length The pointer to the outer length (tag octets + length octets + content length)

  @returns  A value indicating whether the operation succeeded or not
*/
/**************************************************************************/
bool cie_BerReader::readTriple(const cie_EFPath filePath, cie_BerTriple*& triple, word* length) {
  
  byte tagOctets, lengthOctets;
  triple = new cie_BerTriple();
  if (!detectTag(filePath, &triple->classification, &triple->encoding, &triple->type, &tagOctets) ||
      !detectLength(filePath, &triple->contentOffset, &triple->contentLength, &lengthOctets)) {
        return false;
  }
  triple->childrenCount = 0;
  triple->children = new cie_BerTriple*[0];
  *length = tagOctets + lengthOctets + triple->contentLength;

  return true;
}


/**************************************************************************/
/*!
  @brief Reads the binary content of a triple into the buffer
  
  @param triple The triple to be read
  @param buffer The pointer to the buffer where the binary content will be written to

  @returns  A value indicating whether the operation succeeded or not
*/
/**************************************************************************/
bool cie_BerReader::readTripleValue(const cie_BerTriple triple, byte* buffer) {
  resetCursor();
  //TODO leggi valore
  return true;
}


/**************************************************************************/
/*!
  @brief Detects the content length in a triple
  
  @param filePath a structure indicating the parent Dedicated File (either ROOT_MF or CIE_DF), the selection mode (either SELECT_BY_EFID or SELECT_BY_SFI) and the file identifier (either a sfi or an efid)	
  @param contentLength The pointer to binary content length
  @param lengthOctets The pointer to the number of length octets found in this triple)

  @returns  A value indicating whether the operation succeeded or not
*/
/**************************************************************************/
bool cie_BerReader::detectLength(const cie_EFPath filePath, word* contentOffset, word* contentLength, byte* lengthOctets) {  
  if (!readOctet(filePath, lengthOctets)) {
    PN532DEBUGPRINT.println(F("Couldn't detect length of a BER encoded file"));
    return false;
  }
  if (*lengthOctets == 0b10000000)
  {
      //Indefinite mode, we don't currently support this as it's sloooooow
      PN532DEBUGPRINT.println(F("Indefinite length for BER encoded file not supported"));
      return false;
  }
  else if ((*lengthOctets & 0b10000000) == 0b10000000)
  {
      //Definite, long
      //In the initial octet, bit 8 is 1, and bits 1–7 (excluding the values 0 and 127) encode the number of octets that follow.
      *lengthOctets &= 0b1111111;
      if (*lengthOctets == 0 || *lengthOctets == 127) {
        PN532DEBUGPRINT.println(F("Invalid value for a BER encoded file length"));
        return false;
      }
      *contentLength = 0;
      byte* buffer = new byte[*lengthOctets];
      readOctets(filePath, buffer, *lengthOctets);
      //The following octets encode, as big-endian, the length (which may be 0) as a number of octets.
      for (byte i = 0; i < *lengthOctets; i++)
      {
        *contentLength <<= 8;
        *contentLength |= buffer[i];
      }
      //Don't forget to add the first length octet
      *lengthOctets+=1;
      delete [] buffer;
  } else  {
      //Definite, short
      *contentLength = *lengthOctets;
      *lengthOctets = 1;
  }
  *contentOffset = _currentOffset;
  return true;
}


/**************************************************************************/
/*!
  @brief Detects the tag type, class and content encoding
  
  @param filePath a structure indicating the parent Dedicated File (either ROOT_MF or CIE_DF), the selection mode (either SELECT_BY_EFID or SELECT_BY_SFI) and the file identifier (either a sfi or an efid)	
  @param classification The pointer to the detected class value
  @param encoding The pointer to the detected binary content encoding (either primitive or constructed)
  @param type The pointer to the detected type value
  @param tagOctets The pointer to the number of tag octets found in this triple
  @returns  A value indicating whether the operation succeeded or not
*/
/**************************************************************************/
bool cie_BerReader::detectTag (const cie_EFPath filePath, byte* classification, byte* encoding, unsigned int* type, byte* tagOctets) {

  byte tag;
  if (!readOctet(filePath, &tag)) {
    PN532DEBUGPRINT.println(F("Couldn't detect a tag in the BER content"));
    return false;
  }

  //bit 6 encodes whether the type is primitive or constructed
  *encoding = (byte)((tag >> 5) & 0b1);
  //bit 7–8 encode the class of the type
  *classification = (byte)((tag >> 6) & 0b11);
  //bits 1–5 encode the tag number.
  *type = (unsigned int) (tag & 0b11111);

  //Where the identifier is not universal, its tag number may be too large for the 5-bit tag field, so it is encoded in further octets.
  //bits 1–5 are 1
  if (*type == 0b11111)
  {
      *type = 0x00;
      //The tag number is encoded in the following octets, where bit 8 of each is 1 if there are more octets
      bool moreOctets = false;
      *tagOctets = 1;
      do
      {
        *tagOctets += 1;
        if (!readOctet(filePath, &tag)) {
          PN532DEBUGPRINT.println(F("Couldn't detect a tag in the BER content"));
          return false;
        }
        //bits 1–7 encode the tag number. The tag number bits combined, big-endian, encode the tag number
        *type <<= 7;
        *type |= (byte)(tag & 0b1111111);

        //bit 8 of each is 1 if there are more octets
        moreOctets = ((tag & 0b10000000) == 0b10000000);
      } while (moreOctets);
      return true;
  } else {
    *tagOctets = 1;
  }
  return true;
}


/**************************************************************************/
/*!
  @brief Reads bytes from the file
  
  @param filePath a structure indicating the parent Dedicated File (either ROOT_MF or CIE_DF), the selection mode (either SELECT_BY_EFID or SELECT_BY_SFI) and the file identifier (either a sfi or an efid)	
  @param buffer The pointer to the buffer which will contain the octets
  @param offset The offset from which we should read
  @param length The number of bytes to read

  @returns  A value indicating whether the operation succeeded or not
*/
/**************************************************************************/
bool cie_BerReader::readOctets(const cie_EFPath filePath, byte* buffer, const word offset, const word length) {
  if (!_cie->readBinaryContent(filePath, buffer, offset, length)) {
    return false;
  }
  _currentOffset = offset+length;
  return true;
}


/**************************************************************************/
/*!
  @brief Reads bytes from the file
  
  @param filePath a structure indicating the parent Dedicated File (either ROOT_MF or CIE_DF), the selection mode (either SELECT_BY_EFID or SELECT_BY_SFI) and the file identifier (either a sfi or an efid)	
  @param buffer The pointer to the buffer which will contain the octets
  @param length The number of bytes to read

  @returns  A value indicating whether the operation succeeded or not
*/
/**************************************************************************/
bool cie_BerReader::readOctets(const cie_EFPath filePath, byte* buffer, const word length) {
  return readOctets(filePath, buffer, _currentOffset, length);
}

/**************************************************************************/
/*!
  @brief Reads bytes from the file
  
  @param filePath a structure indicating the parent Dedicated File (either ROOT_MF or CIE_DF), the selection mode (either SELECT_BY_EFID or SELECT_BY_SFI) and the file identifier (either a sfi or an efid)	
  @param octet The pointer to the byte read

  @returns  A value indicating whether the operation succeeded or not
*/
/**************************************************************************/
bool cie_BerReader::readOctet(const cie_EFPath filePath, byte* octet) {
  return readOctets(filePath, octet, _currentOffset, 1);
}


/**************************************************************************/
/*!
  @brief Resets the cursor to the binary content
*/
/**************************************************************************/
void cie_BerReader::resetCursor() {
  _currentOffset = 0;
}
