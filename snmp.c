#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "snmp.h"
#include "system_mib.h"

MIBNode *root = NULL;
MIBNode *nodes[MAX_NODES];
int node_count = 0;

MIBNode *add_mib_node(const char *name, const char *oid, const char *type, int isWritable, const char *status, const void *value, MIBNode *parent) {
    for (int i = 0; i < node_count; i++) {
        if (strcmp(nodes[i]->oid, oid) == 0) {
            // printf("Node with OID %s already exists. Skipping...\n", oid);
            return NULL;
        }
    }    
    
    if (node_count >= MAX_NODES) {
        printf("Error: Maximum number of nodes reached.\n");
        return NULL;
    }

    if (strcmp(status, "current") != 0) {
        return NULL;
    }

    // 노드 생성
    MIBNode *node = (MIBNode *)malloc(sizeof(MIBNode));
    if (!node) {
        printf("Error: Memory allocation failed.\n");
        return NULL;
    }

    strncpy(node->name, name, sizeof(node->name) - 1);
    node->name[sizeof(node->name) - 1] = '\0';
    strncpy(node->oid, oid, sizeof(node->oid) - 1);
    node->oid[sizeof(node->oid) - 1] = '\0';
    strncpy(node->type, type, sizeof(node->type) - 1);
    node->type[sizeof(node->type) - 1] = '\0';
    node->isWritable = isWritable;
    strncpy(node->status, status, sizeof(node->status) - 1);
    node->status[sizeof(node->status) - 1] = '\0';
    node->parent = parent;
    node->child = NULL;
    node->next = NULL;

    if (strcmp(type, "Integer32") == 0) {
        node->value_type = VALUE_TYPE_INT;
        node->value.int_value = *(int *)value;
    } else if (strcmp(type, "DisplayString") == 0) {
        node->value_type = VALUE_TYPE_STRING;
        strncpy(node->value.str_value, (const char *)value, sizeof(node->value.str_value) - 1);
        node->value.str_value[sizeof(node->value.str_value) - 1] = '\0';
    } else if (strcmp(type, "TimeTicks") == 0) {
        node->value_type = VALUE_TYPE_TIME_TICKS;
        node->value.ticks_value = *(unsigned long *)value;
    } else if (strcmp(type, "OBJECT IDENTIFIER") == 0 || strcmp(type, "MODULE-IDENTITY") == 0) {
        node->value_type = VALUE_TYPE_OID;
        strncpy(node->value.oid_value, (const char *)value, sizeof(node->value.oid_value) - 1);
        node->value.oid_value[sizeof(node->value.oid_value) - 1] = '\0';
    } else {
        printf("Warning: Unsupported type '%s'. Treating value as a string.\n", type);
        node->value_type = VALUE_TYPE_STRING;
        strncpy(node->value.str_value, (const char *)value, sizeof(node->value.str_value) - 1);
        node->value.str_value[sizeof(node->value.str_value) - 1] = '\0';
    }

    if (parent) {
        if (!parent->child) {
            parent->child = node;
        } else {
            MIBNode *sibling = parent->child;
            while (sibling->next) {
                sibling = sibling->next;
            }
            sibling->next = node;
        }
    }

    if (strcmp(type, "MODULE-IDENTITY") != 0) {
        if (strcmp(type, "OBJECT IDENTIFIER") != 0 || strcmp(name, "sysObjectID") == 0) {
            nodes[node_count++] = node;
        }
    }

    return node;
}

void print_all_mib_nodes() {
    for (int i = 0; i < node_count; i++) {
        MIBNode *node = nodes[i];
        printf("Name: %s\n", node->name);
        printf("  OID: %s\n", node->oid);
        printf("  Type: %s\n", node->type);
        printf("  Writable: %s\n", node->isWritable ? "Yes" : "No");
        printf("  Status: %s\n", node->status);
        
        if (node->value_type == VALUE_TYPE_INT) {
            printf("  Value: %d\n", node->value.int_value);
        } else if (node->value_type == VALUE_TYPE_STRING) {
            printf("  Value: %s\n", node->value.str_value);
        } else if (node->value_type == VALUE_TYPE_OID) {
            printf("  Value (OID): %s\n", node->value.oid_value);
        } else if (node->value_type == VALUE_TYPE_TIME_TICKS) {
            printf("  Value (TimeTicks): %lu\n", node->value.ticks_value);
        } else {
            printf("  Value: Unsupported type\n");
        }
    }
}

// OID 노드를 검색하는 함수
MIBNode *find_mib_node(MIBNode *node, const char *name) {
    if (!node) return NULL;

    if (strcmp(node->name, name) == 0) {
        return node;
    }

    MIBNode *found = find_mib_node(node->child, name);
    if (found) return found;

    return find_mib_node(node->next, name);
}

// OID 노드 추가를 위한 파싱 함수
void parse_object_identifier(char *line) {
    char name[128], parent_name[128];
    int number;

    sscanf(line, "%s OBJECT IDENTIFIER ::= { %s %d }", name, parent_name, &number);

    if (strcmp(parent_name, "cam") != 0) {
        return;
    }

    MIBNode *parent = find_mib_node(root, parent_name);
    if (!parent) {
        printf("Error: Parent OID %s not found for OBJECT IDENTIFIER %s\n", parent_name, name);
        return;
    }

    char full_oid[256];
    snprintf(full_oid, sizeof(full_oid), "%s.%d", parent->oid, number);

    add_mib_node(name, full_oid, "OBJECT IDENTIFIER", 0, "current", "", parent);
}

// OBJECT-TYPE 정의를 파싱하는 함수
void parse_object_type(char *line, FILE *file) {
    char name[128], syntax[128] = "", access[128] = "", status[128] = "", description[256] = "";
    char oid_parent_name[128];
    int oid_number;

    sscanf(line, "%s OBJECT-TYPE", name);

    while (fgets(line, 256, file)) {
        if (strstr(line, "SYNTAX")) {
            sscanf(line, " SYNTAX %s", syntax);
        } else if (strstr(line, "MAX-ACCESS")) {
            sscanf(line, " MAX-ACCESS %s", access);
        } else if (strstr(line, "STATUS")) {
            sscanf(line, " STATUS %s", status);
        } else if (strstr(line, "DESCRIPTION")) {
            char *start = strchr(line, '"');
            if (start) {
                strcpy(description, start + 1);
                char *end = strchr(description, '"');
                if (end) {
                    *end = '\0';
                }
            }
        } else if (strstr(line, "::=")) {
            sscanf(line, " ::= { %s %d }", oid_parent_name, &oid_number);
            break;
        }
    }

    MIBNode *parent = find_mib_node(root, oid_parent_name);
    if (!parent) {
        printf("Error: Parent OID %s not found for OBJECT-TYPE %s\n", oid_parent_name, name);
        return;
    }

    char full_oid[256];
    snprintf(full_oid, sizeof(full_oid), "%s.%d", parent->oid, oid_number);

    int isWritable = (strcmp(access, "read-write") == 0 || strcmp(access, "read-create") == 0);
    add_mib_node(name, full_oid, syntax, isWritable, status, "", parent);
}

void oid_to_string(unsigned char *oid, int oid_len, char *oid_str) {
    int i;
    char buffer[32];

    sprintf(oid_str, "%d.%d", oid[0] / 40, oid[0] % 40);

    for (i = 1; i < oid_len; i++) {
        sprintf(buffer, ".%d", oid[i]);
        strcat(oid_str, buffer);
    }
}

int find_next_mib_entry(unsigned char *oid, int oid_len, MIBNode **nextEntry) {
    char oid_str[BUFFER_SIZE];
    oid_to_string(oid, oid_len, oid_str);

    for (int i = 0; i < node_count; i++) {
        if (strcmp(nodes[i]->oid, oid_str) > 0) {
            *nextEntry = nodes[i];
            return 1;
        }
    }
    *nextEntry = NULL;
    return 0;
}

// ASN.1 BER 형식의 길이 필드 디코딩
int read_length(unsigned char *buffer, int *index) {
    int len = 0;
    unsigned char len_byte = buffer[*index];
    (*index)++;
    if (len_byte & 0x80) {
        int num_len_bytes = len_byte & 0x7F;
        len = 0;
        for (int i = 0; i < num_len_bytes; i++) {
            len = (len << 8) | buffer[*index];
            (*index)++;
        }
    } else {
        len = len_byte;
    }
    return len;
}

int read_integer(unsigned char *buffer, int *index, int len) {
    int value = 0;
    for (int i = 0; i < len; i++) {
        value = (value << 8) | buffer[*index];
        (*index)++;
    }
    return value;
}

// ASN.1 BER 형식의 길이 필드 인코딩
int write_length(unsigned char *buffer, int len) {
    if (len < 0) {
        printf("len < 0\n");
        return -1; // Return error for invalid length
    }
    if (len < 128) {
        printf("len < 128\n");
        buffer[0] = len;
        return 1;
    } else {
        int num_len_bytes = 0;
        int temp_len = len;
        unsigned char len_bytes[4];
        while (temp_len > 0) {
            len_bytes[num_len_bytes++] = temp_len & 0xFF;
            temp_len >>= 8;
        }
        buffer[0] = 0x80 | num_len_bytes;
        for (int i = 0; i < num_len_bytes; i++) {
            buffer[i + 1] = len_bytes[num_len_bytes - 1 - i];
        }
        return num_len_bytes + 1;
    }
}

void parse_tlv(unsigned char *buffer, int *index, int length, SNMPPacket *snmp_packet) {
    while (*index < length) {
        unsigned char type = buffer[*index];
        (*index)++;
        int len = read_length(buffer, index);
        int value_start = *index;

        if (type == 0x30 || (type >= 0xA0 && type <= 0xA5)) {  // SEQUENCE 또는 PDU
            if (type >= 0xA0 && type <= 0xA5) {
                snmp_packet->pdu_type = type;  // PDU 타입 저장
            }
            int new_index = *index;
            parse_tlv(buffer, &new_index, value_start + len, snmp_packet);  // 내부 SEQUENCE 파싱
            *index = value_start + len;  // 인덱스 업데이트
        } else if (type == 0x02) {  // INTEGER
            if (snmp_packet->version == -1) {
                snmp_packet->version = buffer[*index];  // SNMP 버전 저장
            } else if (snmp_packet->pdu_type == 0xA5) {  // GET-BULK PDU
                if (snmp_packet->request_id == 0) {
                    snmp_packet->request_id = (buffer[*index] << 24) | (buffer[*index + 1] << 16) | (buffer[*index + 2] << 8) | buffer[*index + 3];  // Request ID 저장
                } else if (snmp_packet->max_repetitions == 0) {
                    snmp_packet->max_repetitions = buffer[*index];  // non-repeaters 저장
                } else if (snmp_packet->non_repeaters == 0) {
                    snmp_packet->non_repeaters = buffer[*index];  // max-repetitions 저장
                }
            } else if (len == 4) {
                snmp_packet->request_id = (buffer[*index] << 24) | (buffer[*index + 1] << 16) | (buffer[*index + 2] << 8) | buffer[*index + 3];  // Request ID 저장
            } else if (len == 1 && snmp_packet->error_status == 0) {
                snmp_packet->error_status = buffer[*index];  // Error Status
            } else if (len == 1 && snmp_packet->error_index == 0) {
                snmp_packet->error_index = buffer[*index];  // Error Index
            }
            *index += len;
        } else if (type == 0x04) {  // OCTET STRING
            if (snmp_packet->community[0] == '\0') {
                memcpy(snmp_packet->community, &buffer[*index], len);  // 커뮤니티 이름 저장
                snmp_packet->community[len] = '\0';  // NULL 종료
            }
            *index += len;
        } else if (type == 0x06) {  // OID
            memcpy(snmp_packet->oid, &buffer[*index], len);  // OID 저장
            snmp_packet->oid_len = len;
            *index += len;
        } else {
            *index += len;
        }
    }
}

void parse_pdu(unsigned char *buffer, int *index, int length, SNMPv3Packet *snmp_packet, unsigned char pdu_type) {
    snmp_packet->pdu_type = pdu_type;

    int pdu_end = *index + length;

    // 1. request-id
    if (*index >= pdu_end) {
        printf("Index out of bounds while reading request-id\n");
        return;
    }
    unsigned char type = buffer[*index];
    if (type != 0x02) {
        printf("Invalid request-id Type\n");
        return;
    }
    (*index)++;
    int len = read_length(buffer, index);
    if (*index + len > pdu_end) {
        printf("Invalid length for request-id\n");
        return;
    }
    snmp_packet->request_id = read_integer(buffer, index, len);

    // 2. error-status
    if (*index >= pdu_end) {
        printf("Index out of bounds while reading error-status\n");
        return;
    }
    type = buffer[*index];
    if (type != 0x02) {
        printf("Invalid error-status Type\n");
        return;
    }
    (*index)++;
    len = read_length(buffer, index);
    if (*index + len > pdu_end) {
        printf("Invalid length for error-status\n");
        return;
    }
    snmp_packet->error_status = read_integer(buffer, index, len);

    // 3. error-index
    if (*index >= pdu_end) {
        printf("Index out of bounds while reading error-index\n");
        return;
    }
    type = buffer[*index];
    if (type != 0x02) {
        printf("Invalid error-index Type\n");
        return;
    }
    (*index)++;
    len = read_length(buffer, index);
    if (*index + len > pdu_end) {
        printf("Invalid length for error-index\n");
        return;
    }
    snmp_packet->error_index = read_integer(buffer, index, len);

    // 4. variable-bindings
    if (*index >= pdu_end) {
        printf("Index out of bounds while reading variable-bindings\n");
        return;
    }
    type = buffer[*index];
    if (type != 0x30) {
        printf("Invalid variable-bindings Type\n");
        return;
    }
    (*index)++;
    len = read_length(buffer, index);
    if (*index + len > pdu_end) {
        printf("Invalid length for variable-bindings\n");
        return;
    }
    int varbind_list_end = *index + len;

    snmp_packet->varbind_count = 0;

    // VarBindList 파싱
    while (*index < varbind_list_end) {
        // VarBind SEQUENCE
        if (*index >= varbind_list_end) {
            printf("Index out of bounds while reading VarBind SEQUENCE\n");
            return;
        }
        type = buffer[*index];
        if (type != 0x30) {
            printf("Invalid VarBind Type\n");
            return;
        }
        (*index)++;
        len = read_length(buffer, index);
        if (*index + len > varbind_list_end) {
            printf("Invalid length for VarBind SEQUENCE\n");
            return;
        }
        int varbind_end = *index + len;

        // OID 파싱
        if (*index >= varbind_end) {
            printf("Index out of bounds while reading OID\n");
            return;
        }
        type = buffer[*index];
        if (type != 0x06) {
            printf("Invalid OID Type\n");
            return;
        }
        (*index)++;
        len = read_length(buffer, index);
        if (*index + len > varbind_end) {
            printf("Invalid length for OID\n");
            return;
        }
        memcpy(snmp_packet->varbind_list[snmp_packet->varbind_count].oid, &buffer[*index], len);
        snmp_packet->varbind_list[snmp_packet->varbind_count].oid_len = len;
        (*index) += len;

        // Value 파싱
        if (*index >= varbind_end) {
            printf("Index out of bounds while reading Value Type\n");
            return;
        }
        type = buffer[*index];
        (*index)++;
        len = read_length(buffer, index);
        if (*index + len > varbind_end) {
            printf("Invalid length for Value\n");
            return;
        }
        snmp_packet->varbind_list[snmp_packet->varbind_count].value_type = type;
        memcpy(snmp_packet->varbind_list[snmp_packet->varbind_count].value, &buffer[*index], len);
        snmp_packet->varbind_list[snmp_packet->varbind_count].value_len = len;
        (*index) += len;

        snmp_packet->varbind_count++;

        if (*index != varbind_end) {
            printf("VarBind SEQUENCE length mismatch\n");
            return;
        }
    }

    if (*index != pdu_end) {
        printf("PDU length mismatch\n");
    }
}

void parse_scoped_pdu(unsigned char *buffer, int *index, int length, SNMPv3Packet *snmp_packet) {
    unsigned char type;
    int len;

    // ScopedPDU SEQUENCE
    if (*index >= length) {
        printf("Index out of bounds while reading ScopedPDU\n");
        return;
    }
    type = buffer[*index];
    (*index)++;
    if (type != 0x30) {
        printf("Invalid ScopedPDU Type\n");
        return;
    }

    len = read_length(buffer, index);
    if (len < 0 || *index + len > length) {
        printf("Invalid length for ScopedPDU\n");
        return;
    }
    int seq_end = (*index) + len;

    // 1. contextEngineID`
    if (*index >= seq_end) {
        printf("Index out of bounds while reading contextEngineID\n");
        return;
    }
    type = buffer[*index];
    (*index)++;
    if (type != 0x04) {
        printf("Invalid contextEngineID Type\n");
        return;
    }

    len = read_length(buffer, index);
    if (len < 0 || *index + len > seq_end) {
        printf("Invalid length for contextEngineID\n");
        return;
    }
    memcpy(snmp_packet->contextEngineID, &buffer[*index], len);
    snmp_packet->contextEngineID_len = len;
    (*index) += len;

    // 2. contextName
    if (*index >= seq_end) {
        printf("Index out of bounds while reading contextName\n");
        return;
    }
    type = buffer[*index];
    (*index)++;
    if (type != 0x04) {
        printf("Invalid contextName Type\n");
        return;
    }

    len = read_length(buffer, index);
    if (len < 0 || *index + len > seq_end) {
        printf("Invalid length for contextName\n");
        return;
    }
    memcpy(snmp_packet->contextName, &buffer[*index], len);
    snmp_packet->contextName[len] = '\0';
    (*index) += len;

    // data (PDU) 파싱
    if (*index >= seq_end) {
        printf("Index out of bounds while reading data PDU\n");
        return;
    }
    unsigned char pdu_type = buffer[*index];
    (*index)++;
    len = read_length(buffer, index);
    if (len < 0 || *index + len > seq_end) {
        printf("Invalid length for data PDU\n");
        return;
    }

    parse_pdu(buffer, index, len, snmp_packet, pdu_type);
}


void parse_usm_security_parameters(unsigned char *buffer, int *index, int length, SNMPv3Packet *snmp_packet) {
    unsigned char type;
    int len;

    // USM SEQUENCE
    if (*index >= length) {
        printf("Index out of bounds while reading USM Sequence\n");
        return;
    }
    type = buffer[*index];
    (*index)++;
    if (type != 0x30) {
        printf("Invalid USM Sequence Type\n");
        return;
    }
    len = read_length(buffer, index);
    if (len < 0 || *index + len > length) {
        printf("Invalid length for USM Sequence\n");
        return;
    }
    int seq_end = (*index) + len;

    // 1. msgAuthoritativeEngineID
    if (*index >= length) {
        printf("Index out of bounds while reading msgAuthoritativeEngineID\n");
        return;
    }
    type = buffer[*index];
    (*index)++;
    if (type != 0x04) {
        printf("Invalid msgAuthoritativeEngineID Type\n");
        return;
    }
    len = read_length(buffer, index);
    if (len < 0 || *index + len > length) {
        printf("Invalid length for msgAuthoritativeEngineID\n");
        return;
    }
    memcpy(snmp_packet->msgAuthoritativeEngineID, &buffer[*index], len);
    snmp_packet->msgAuthoritativeEngineID_len = len;
    (*index) += len;

    // 2. msgAuthoritativeEngineBoots
    if (*index >= length) {
        printf("Index out of bounds while reading msgAuthoritativeEngineBoots\n");
        return;
    }
    type = buffer[*index];
    (*index)++;
    if (type != 0x02) {
        printf("Invalid msgAuthoritativeEngineBoots Type\n");
        return;
    }
    len = read_length(buffer, index);
    snmp_packet->msgAuthoritativeEngineBoots = read_integer(buffer, index, len);

    // 3. msgAuthoritativeEngineTime
    if (*index >= length) {
        printf("Index out of bounds while reading msgAuthoritativeEngineTime\n");
        return;
    }
    type = buffer[*index];
    (*index)++;
    if (type != 0x02) {
        printf("Invalid msgAuthoritativeEngineTime Type\n");
        return;
    }
    len = read_length(buffer, index);
    snmp_packet->msgAuthoritativeEngineTime = read_integer(buffer, index, len);

    // 4. msgUserName
    if (*index >= length) {
        printf("Index out of bounds while reading msgUserName\n");
        return;
    }
    type = buffer[*index];
    (*index)++;
    if (type != 0x04) {
        printf("Invalid msgUserName Type\n");
        return;
    }
    len = read_length(buffer, index);
    if (len < 0 || *index + len > length) {
        printf("Invalid length for msgUserName\n");
        return;
    }
    memcpy(snmp_packet->msgUserName, &buffer[*index], len);
    snmp_packet->msgUserName[len] = '\0';
    (*index) += len;

    // 5. msgAuthenticationParameters
    if (*index >= length) {
        printf("Index out of bounds while reading msgAuthenticationParameters\n");
        return;
    }
    type = buffer[*index];
    (*index)++;
    if (type != 0x04) {
        printf("Invalid msgAuthenticationParameters Type\n");
        return;
    }
    len = read_length(buffer, index);
    if (len < 0 || *index + len > length) {
        printf("Invalid length for msgAuthenticationParameters\n");
        return;
    }
    memcpy(snmp_packet->msgAuthenticationParameters, &buffer[*index], len);
    snmp_packet->msgAuthenticationParameters_len = len;
    (*index) += len;

    // 6. msgPrivacyParameters
    if (*index >= length) {
        printf("Index out of bounds while reading msgPrivacyParameters\n");
        return;
    }
    type = buffer[*index];
    (*index)++;
    if (type != 0x04) {
        printf("Invalid msgPrivacyParameters Type\n");
        return;
    }
    len = read_length(buffer, index);
    if (len < 0 || *index + len > length) {
        printf("Invalid length for msgPrivacyParameters\n");
        return;
    }
    memcpy(snmp_packet->msgPrivacyParameters, &buffer[*index], len);
    snmp_packet->msgPrivacyParameters_len = len;
    (*index) += len;
}

void parse_snmpv3_message(unsigned char *buffer, int *index, int length, SNMPv3Packet *snmp_packet) {
    unsigned char type;
    int len;

    // 1. SNMPv3Message (SEQUENCE)
    if (*index >= length) {
        printf("Index out of bounds while reading SNMPv3 Message Type\n");
        return;
    }
    type = buffer[*index];
    (*index)++;
    if (type != 0x30) {
        printf("Invalid SNMPv3 Message Type\n");
        return;
    }
    
    len = read_length(buffer, index);
    if (len < 0 || *index + len > length) {
        printf("Invalid length for SNMPv3 Message\n");
        return;
    }
    int seq_end = *index + len;

    // 2. msgVersion
    if (*index >= length) {
        printf("Index out of bounds while reading msgVersion\n");
        return;
    }
    type = buffer[*index];
    (*index)++;
    if (type != 0x02) {
        printf("Invalid msgVersion Type\n");
        return;
    }
    len = read_length(buffer, index);
    snmp_packet->version = read_integer(buffer, index, len);

    // 3. msgGlobalData (HeaderData)
    if (*index >= length) {
        printf("Index out of bounds while reading msgGlobalData\n");
        return;
    }
    type = buffer[*index];
    (*index)++;
    if (type != 0x30) {
        printf("Invalid msgGlobalData Type\n");
        return;
    }
    len = read_length(buffer, index);
    if (len < 0 || *index + len > length) {
        printf("Invalid length for msgGlobalData\n");
        return;
    }
    int header_end = *index + len;

    // 3.1 msgID
    if (*index >= length) {
        printf("Index out of bounds while reading msgID\n");
        return;
    }
    type = buffer[*index];
    (*index)++;
    if (type != 0x02) {
        printf("Invalid msgID Type\n");
        return;
    }
    len = read_length(buffer, index);
    snmp_packet->msgID = read_integer(buffer, index, len);

    // 3.2 msgMaxSize
    if (*index >= length) {
        printf("Index out of bounds while reading msgMaxSize\n");
        return;
    }
    type = buffer[*index];
    (*index)++;
    if (type != 0x02) {
        printf("Invalid msgMaxSize Type\n");
        return;
    }
    len = read_length(buffer, index);
    snmp_packet->msgMaxSize = read_integer(buffer, index, len);

    // 3.3 msgFlags
    if (*index >= length) {
        printf("Index out of bounds while reading msgFlags\n");
        return;
    }
    type = buffer[*index];
    (*index)++;
    if (type != 0x04) {
        printf("Invalid msgFlags Type\n");
        return;
    }
    len = read_length(buffer, index);
    if (len < 0 || *index + len > length) {
        printf("Invalid length for msgFlags\n");
        return;
    }
    memcpy(snmp_packet->msgFlags, &buffer[*index], len);
    (*index) += len;

    // 3.4 msgSecurityModel
    if (*index >= length) {
        printf("Index out of bounds while reading msgSecurityModel\n");
        return;
    }
    type = buffer[*index];
    (*index)++;
    if (type != 0x02) {
        printf("Invalid msgSecurityModel Type\n");
        return;
    }
    len = read_length(buffer, index);
    snmp_packet->msgSecurityModel = read_integer(buffer, index, len);

    // 4. msgSecurityParameters
    if (*index >= length) {
        printf("Index out of bounds while reading msgSecurityParameters\n");
        return;
    }
    type = buffer[*index];
    (*index)++;
    if (type != 0x04) { // OCTET STRING 타입 확인
        printf("Invalid msgSecurityParameters Type\n");
        return;
    }
    len = read_length(buffer, index);
    if (len < 0 || *index + len > length) {
        printf("Invalid length for msgSecurityParameters\n");
        return;
    }

    // msgSecurityParameters를 임시 버퍼에 저장하여 파싱
    unsigned char sec_params_buffer[BUFFER_SIZE];
    memcpy(sec_params_buffer, &buffer[*index], len);
    (*index) += len;

    int sec_params_index = 0;
    parse_usm_security_parameters(sec_params_buffer, &sec_params_index, len, snmp_packet);

    // 5. msgData (ScopedPDUData)
    if (*index >= length) {
        printf("Index out of bounds while reading msgData\n");
        return;
    }
    type = buffer[*index];
    (*index)++;
    if (type == 0x04) { // OCTET STRING (plaintext)
        len = read_length(buffer, index);
        if (len < 0 || *index + len > length) {
            printf("Invalid length for msgData OCTET STRING\n");
            return;
        }

        // ScopedPDU 파싱
        unsigned char scoped_pdu_buffer[BUFFER_SIZE];
        memcpy(scoped_pdu_buffer, &buffer[*index], len);
        (*index) += len;

        int scoped_pdu_index = 0;
        // 여기서는 인덱스를 0으로 설정하고, ScopedPDU의 길이를 len으로 설정하여 파싱
        parse_scoped_pdu(scoped_pdu_buffer, &scoped_pdu_index, len, snmp_packet);
    } else if (type == 0x30) { // SEQUENCE (ScopedPDU directly)
        (*index)--; // 타입 바이트를 다시 읽기 위해 인덱스 감소
        int scoped_pdu_start = *index; // ScopedPDU 시작 위치
        int remaining_length = length - scoped_pdu_start;

        // parse_scoped_pdu를 호출할 때 index를 0으로 설정하고, length를 remaining_length로 설정합니다.
        int scoped_pdu_index = 0;
        parse_scoped_pdu(&buffer[scoped_pdu_start], &scoped_pdu_index, remaining_length, snmp_packet);
    } else {
        printf("Invalid msgData Type: %02X\n", type);
        return;
    }
}


int string_to_oid(const char *oid_str, unsigned char *oid_buf) {
    int oid_buf_len = 0;
    unsigned int oid_parts[128];
    int oid_parts_count = 0;

    // OID 문자열을 정수 배열로 파싱
    char oid_copy[BUFFER_SIZE];
    strcpy(oid_copy, oid_str);
    char *token = strtok(oid_copy, ".");
    while (token != NULL && oid_parts_count < 128) {
        oid_parts[oid_parts_count++] = atoi(token);
        token = strtok(NULL, ".");
    }

    if (oid_parts_count < 2) {
        return 0;
    }

    oid_buf[oid_buf_len++] = (unsigned char)(oid_parts[0] * 40 + oid_parts[1]);

    for (int i = 2; i < oid_parts_count; i++) {
        unsigned int value = oid_parts[i];
        unsigned char temp[5];
        int temp_len = 0;

        do {
            temp[temp_len++] = value & 0x7F;
            value >>= 7;
        } while (value > 0);

        for (int j = temp_len - 1; j >= 0; j--) {
            unsigned char byte = temp[j];
            if (j != 0)
                byte |= 0x80;
            oid_buf[oid_buf_len++] = byte;
        }
    }

    return oid_buf_len;
}

int encode_length(unsigned char *buffer, int length) {
    if (length < 128) {
        buffer[0] = length;
        return 1;
    } else {
        int len = length;
        int num_bytes = 0;
        unsigned char len_bytes[4];
        while (len > 0) {
            len_bytes[num_bytes++] = len & 0xFF;
            len >>= 8;
        }
        buffer[0] = 0x80 | num_bytes;
        for (int i = 0; i < num_bytes; i++) {
            buffer[i + 1] = len_bytes[num_bytes - 1 - i];
        }
        return num_bytes + 1;
    }
}

int encode_oid(const oid *oid_numbers, int oid_len, unsigned char *buffer) {
    int buf_len = 0;

    if (oid_len < 2) {
        return 0; // Invalid OID
    }

    buffer[buf_len++] = (unsigned char)(oid_numbers[0] * 40 + oid_numbers[1]);

    for (int i = 2; i < oid_len; i++) {
        unsigned long value = oid_numbers[i];
        unsigned char temp[10];
        int temp_len = 0;

        do {
            temp[temp_len++] = value & 0x7F;
            value >>= 7;
        } while (value > 0);

        for (int j = temp_len - 1; j >= 0; j--) {
            buffer[buf_len++] = temp[j] | (j != 0 ? 0x80 : 0x00);
        }
    }

    return buf_len;
}

int encode_length_at(unsigned char *buffer, int length) {
    unsigned char temp[10];
    int len_bytes = encode_length(temp, length);

    // Move existing data to make space for length bytes
    memmove(buffer + len_bytes, buffer + 1, length);

    // Copy the length bytes into the buffer
    memcpy(buffer, temp, len_bytes);

    return len_bytes;
}


int encode_integer(long value, unsigned char *buffer) {
    int buf_len = 0;
    unsigned long val = (unsigned long)value;

    // Determine the number of bytes needed
    int num_bytes = 0;
    unsigned long temp = val;
    do {
        temp >>= 8;
        num_bytes++;
    } while (temp > 0);

    // If the most significant bit is 1, prepend a zero byte
    if ((val >> ((num_bytes - 1) * 8)) & 0x80) {
        buffer[buf_len++] = 0x00;
        num_bytes++;
    }

    for (int i = num_bytes - 1; i >= 0; i--) {
        buffer[buf_len++] = (val >> (i * 8)) & 0xFF;
    }

    return buf_len;
}


int oid_compare(const unsigned char *oid1, int oid1_len, const unsigned char *oid2, int oid2_len) {
    int min_len = oid1_len < oid2_len ? oid1_len : oid2_len;

    for (int i = 0; i < min_len; i++) {
        if (oid1[i] < oid2[i]) {
            return -1;
        } else if (oid1[i] > oid2[i]) {
            return 1;
        }
    }

    if (oid1_len < oid2_len) {
        return -1;
    } else if (oid1_len > oid2_len) {
        return 1;
    }

    return 0;
}

void generate_engine_id(unsigned char *engine_id) {
    unsigned char enterprise_oid[] = {0x80, 0x00, 0x0, 0x7F};
    // unsigned char enterprise_oid[] = {0x80, 0x00, 0x1F, 0x88};
    char *mac_str = get_mac_address();
    
    if (mac_str == NULL) {
        fprintf(stderr, "Failed to get MAC address\n");
        exit(1);
    }

    // 엔터프라이즈 OID 복사
    memcpy(engine_id, enterprise_oid, sizeof(enterprise_oid));

    // MAC 주소 문자열을 16진수로 변환하여 엔진 ID에 추가
    for (int i = 0; i < 6; i++) {
        unsigned int byte;
        sscanf(mac_str + (i * 3), "%02x", &byte);
        engine_id[sizeof(enterprise_oid) + i] = (unsigned char) byte;
    }
}

void create_snmpv3_report_response(SNMPv3Packet *request_packet, unsigned char *response, int *response_len, int error) {
    // Report PDU OIDs
    static const oid unknownSecurityLevel[] = {1, 3, 6, 1, 6, 3, 15, 1, 1, 1, 0};
    static const oid notInTimeWindow[]      = {1, 3, 6, 1, 6, 3, 15, 1, 1, 2, 0};
    static const oid unknownUserName[]      = {1, 3, 6, 1, 6, 3, 15, 1, 1, 3, 0};
    static const oid unknownEngineID[]      = {1, 3, 6, 1, 6, 3, 15, 1, 1, 4, 0};
    static const oid wrongDigest[]          = {1, 3, 6, 1, 6, 3, 15, 1, 1, 5, 0};
    static const oid decryptionError[]      = {1, 3, 6, 1, 6, 3, 15, 1, 1, 6, 0};

    const oid *err_oid;
    int err_oid_len;

    // Choose the appropriate error OID based on the error type
    switch (error) {
        case SNMPERR_USM_UNKNOWNENGINEID:
            err_oid = unknownEngineID;
            err_oid_len = sizeof(unknownEngineID) / sizeof(oid);
            break;
        case SNMPERR_USM_UNKNOWNSECURITYNAME:
            err_oid = unknownUserName;
            err_oid_len = sizeof(unknownUserName) / sizeof(oid);
            break;
        case SNMPERR_USM_UNSUPPORTEDSECURITYLEVEL:
            err_oid = unknownSecurityLevel;
            err_oid_len = sizeof(unknownSecurityLevel) / sizeof(oid);
            break;
        case SNMPERR_USM_AUTHENTICATIONFAILURE:
            err_oid = wrongDigest;
            err_oid_len = sizeof(wrongDigest) / sizeof(oid);
            break;
        case SNMPERR_USM_NOTINTIMEWINDOW:
            err_oid = notInTimeWindow;
            err_oid_len = sizeof(notInTimeWindow) / sizeof(oid);
            break;
        case SNMPERR_USM_DECRYPTIONERROR:
            err_oid = decryptionError;
            err_oid_len = sizeof(decryptionError) / sizeof(oid);
            break;
        default:
            printf("Unknown SNMPv3 error type: %d\n", error);
            *response_len = 0;
            return;
    }

    // Agent's own Engine ID
    // static const unsigned char engine_id[] = {
    //     // Example Engine ID, should be unique for your agent
    //     0x80, 0x00, 0x1F, 0x88, 0x80, 0x41, 0x17, 0xB5,
    //     0x74, 0xA4, 0xAA, 0xDF, 0x66
    // };

    unsigned char engine_id[10]; // Adjust size based on generate_engine_id output length
    generate_engine_id(engine_id);

    int engine_id_len = sizeof(engine_id);

    // Report PDU construction
    unsigned char report_pdu[BUFFER_SIZE];
    int pdu_len = 0;

    report_pdu[pdu_len++] = 0xA8; // REPORT PDU
    int pdu_length_pos = pdu_len++; // PDU length position placeholder

    // Request ID
    unsigned char request_id_buf[5];
    int request_id_len = encode_integer(request_packet->request_id, request_id_buf);

    report_pdu[pdu_len++] = 0x02; // INTEGER
    pdu_len += encode_length(&report_pdu[pdu_len], request_id_len);
    memcpy(&report_pdu[pdu_len], request_id_buf, request_id_len);
    pdu_len += request_id_len;

    // Error Status
    report_pdu[pdu_len++] = 0x02; // INTEGER
    report_pdu[pdu_len++] = 0x01; // Length
    report_pdu[pdu_len++] = 0x00; // noError

    // Error Index
    report_pdu[pdu_len++] = 0x02; // INTEGER
    report_pdu[pdu_len++] = 0x01; // Length
    report_pdu[pdu_len++] = 0x00; // noError

    // Variable Bindings
    report_pdu[pdu_len++] = 0x30; // SEQUENCE
    int varbind_list_len_pos = pdu_len++; // Length placeholder

    // Variable Binding
    report_pdu[pdu_len++] = 0x30; // SEQUENCE
    int varbind_len_pos = pdu_len++; // Length placeholder

    // OID
    unsigned char oid_buffer[64];
    int oid_encoded_len = encode_oid(err_oid, err_oid_len, oid_buffer);

    report_pdu[pdu_len++] = 0x06; // OBJECT IDENTIFIER
    pdu_len += encode_length(&report_pdu[pdu_len], oid_encoded_len);
    memcpy(&report_pdu[pdu_len], oid_buffer, oid_encoded_len);
    pdu_len += oid_encoded_len;

    // Value (Counter32 with value 1)
    report_pdu[pdu_len++] = 0x41; // Counter32
    unsigned char error_counter_buf[5];
    int error_counter_len = encode_integer(1, error_counter_buf);

    pdu_len += encode_length(&report_pdu[pdu_len], error_counter_len);
    memcpy(&report_pdu[pdu_len], error_counter_buf, error_counter_len);
    pdu_len += error_counter_len;

    // Variable Binding Length
    int varbind_len = pdu_len - varbind_len_pos - 1;
    int len_bytes_varbind  = encode_length_at(&report_pdu[varbind_len_pos], varbind_len);
    pdu_len += (len_bytes_varbind  - 1);

    // Variable Bindings Length
    int varbind_list_len = pdu_len - varbind_list_len_pos - 1;
    len_bytes_varbind  = encode_length_at(&report_pdu[varbind_list_len_pos], varbind_list_len);
    pdu_len += (len_bytes_varbind  - 1);

    // PDU Length
    int pdu_content_length = pdu_len - pdu_length_pos - 1;
    len_bytes_varbind  = encode_length_at(&report_pdu[pdu_length_pos], pdu_content_length);
    pdu_len += (len_bytes_varbind  - 1);

    // SNMPv3 Message construction
    unsigned char buffer[BUFFER_SIZE];
    int index = 0;

    buffer[index++] = 0x30; // SEQUENCE
    int snmp_msg_length_pos = index++; // Length placeholder

    // msgVersion
    buffer[index++] = 0x02; // INTEGER
    buffer[index++] = 0x01; // Length
    buffer[index++] = 0x03; // Version 3

    // msgGlobalData
    buffer[index++] = 0x30; // SEQUENCE
    int global_data_length_pos = index++; // Length placeholder

    // msgID
    unsigned char msg_id_buf[5];
    int msg_id_len = encode_integer(request_packet->msgID, msg_id_buf);

    buffer[index++] = 0x02; // INTEGER
    index += encode_length(&buffer[index], msg_id_len);
    memcpy(&buffer[index], msg_id_buf, msg_id_len);
    index += msg_id_len;

    // msgMaxSize
    unsigned char msg_max_size_buf[5];
    int msg_max_size_len = encode_integer(request_packet->msgMaxSize, msg_max_size_buf);

    buffer[index++] = 0x02; // INTEGER
    index += encode_length(&buffer[index], msg_max_size_len);
    memcpy(&buffer[index], msg_max_size_buf, msg_max_size_len);
    index += msg_max_size_len;

    // msgFlags
    buffer[index++] = 0x04; // OCTET STRING
    buffer[index++] = 0x01; // Length

    // msgFlags 초기화 (reportableFlag 설정)
    unsigned char msg_flags = 0x04;

    // 오류 유형에 따른 보안 수준 설정
    if (error == SNMPERR_USM_UNKNOWNENGINEID) {
        // 인증 필요 (authNoPriv)
        msg_flags = 0x00;
    } else {
        // noAuthNoPriv (기본값)
        // msg_flags는 이미 0x04로 설정되어 있음
    }

    // msgFlags 적용
    buffer[index++] = msg_flags;

    // msgSecurityModel
    buffer[index++] = 0x02; // INTEGER
    buffer[index++] = 0x01; // Length
    buffer[index++] = request_packet->msgSecurityModel;

    // msgGlobalData Length
    int global_data_length = index - global_data_length_pos - 1;
    len_bytes_varbind  = encode_length_at(&buffer[global_data_length_pos], global_data_length);
    index += (len_bytes_varbind  - 1);

    // msgSecurityParameters
    buffer[index++] = 0x04; // OCTET STRING
    int sec_params_length_pos = index++; // Length placeholder

    // Encode USM Security Parameters
    unsigned char sec_params_buffer[BUFFER_SIZE];
    int sec_params_index = 0;

    sec_params_buffer[sec_params_index++] = 0x30; // SEQUENCE
    int usm_length_pos = sec_params_index++; // Length placeholder

    // msgAuthoritativeEngineID (Agent's own engine ID)
    sec_params_buffer[sec_params_index++] = 0x04; // OCTET STRING
    sec_params_index += encode_length(&sec_params_buffer[sec_params_index], engine_id_len);
    memcpy(&sec_params_buffer[sec_params_index], engine_id, engine_id_len);
    sec_params_index += engine_id_len;

    // msgAuthoritativeEngineBoots (set to 0)
    sec_params_buffer[sec_params_index++] = 0x02; // INTEGER
    unsigned char boots_buf[5];
    int boots_len = encode_integer(48, boots_buf);
    sec_params_index += encode_length(&sec_params_buffer[sec_params_index], boots_len);
    memcpy(&sec_params_buffer[sec_params_index], boots_buf, boots_len);
    sec_params_index += boots_len;

    // msgAuthoritativeEngineTime (set to 0)
    sec_params_buffer[sec_params_index++] = 0x02; // INTEGER
    unsigned char time_buf[5];
    int time_len = encode_integer(2885, time_buf);
    sec_params_index += encode_length(&sec_params_buffer[sec_params_index], time_len);
    memcpy(&sec_params_buffer[sec_params_index], time_buf, time_len);
    sec_params_index += time_len;

    // msgUserName (empty string)
    sec_params_buffer[sec_params_index++] = 0x04; // OCTET STRING
    sec_params_buffer[sec_params_index++] = 0x00; // Length
    // No user name to copy

    // msgAuthenticationParameters (empty string)
    sec_params_buffer[sec_params_index++] = 0x04; // OCTET STRING
    sec_params_buffer[sec_params_index++] = 0x00; // Length
    // No auth parameters

    // msgPrivacyParameters (empty string)
    sec_params_buffer[sec_params_index++] = 0x04; // OCTET STRING
    sec_params_buffer[sec_params_index++] = 0x00; // Length
    // No privacy parameters

    // Update USM length
    int usm_length = sec_params_index - usm_length_pos - 1;
    len_bytes_varbind  = encode_length_at(&sec_params_buffer[usm_length_pos], usm_length);
    sec_params_index += (len_bytes_varbind  - 1);

    // Copy Security Parameters to buffer
    memcpy(&buffer[index], sec_params_buffer, sec_params_index);
    index += sec_params_index;

    // Calculate length of msgSecurityParameters
    int sec_params_length = index - sec_params_length_pos - 1;
    len_bytes_varbind  = encode_length_at(&buffer[sec_params_length_pos], sec_params_length);
    index += (len_bytes_varbind  - 1);

    // msgData (ScopedPDUData)
    // 암호화를 사용하지 않으므로 ScopedPDU를 직접 포함
    int scoped_pdu_start = index; // ScopedPDU 시작 위치

    // ScopedPDU 생성
    unsigned char scoped_pdu[BUFFER_SIZE];
    int scoped_index = 0;

    // ScopedPDU SEQUENCE 시작
    scoped_pdu[scoped_index++] = 0x30; // SEQUENCE
    int scoped_pdu_length_pos = scoped_index++; // Length placeholder

    // contextEngineID (에이전트의 엔진 ID)
    scoped_pdu[scoped_index++] = 0x04; // OCTET STRING
    scoped_pdu[scoped_index++] = engine_id_len;
    memcpy(&scoped_pdu[scoped_index], engine_id, engine_id_len);
    scoped_index += engine_id_len;

    // contextName (빈 문자열)
    scoped_pdu[scoped_index++] = 0x04; // OCTET STRING
    scoped_pdu[scoped_index++] = 0x00;
    // contextName 없음

    // data (Report PDU)
    memcpy(&scoped_pdu[scoped_index], report_pdu, pdu_len);
    scoped_index += pdu_len;

    // ScopedPDU 길이 설정
    int scoped_pdu_length = scoped_index - scoped_pdu_length_pos - 1;
    int len_bytes = encode_length_at(&scoped_pdu[scoped_pdu_length_pos], scoped_pdu_length);
    scoped_index += (len_bytes - 1);

    // ScopedPDU를 메인 버퍼에 복사
    memcpy(&buffer[index], scoped_pdu, scoped_index);
    index += scoped_index;

    // msgData 길이 계산 및 설정이 필요하지 않음 (ScopedPDU를 직접 포함하므로)

    // SNMPv3Message 전체 길이 설정
    int snmp_msg_length = index - snmp_msg_length_pos - 1;
    len_bytes = encode_length_at(&buffer[snmp_msg_length_pos], snmp_msg_length);
    index += (len_bytes - 1);

    // Copy the response to the output
    *response_len = index;
    memcpy(response, buffer, index);
}


void create_bulk_response(SNMPPacket *request_packet, unsigned char *response, int *response_len, MIBNode *mibEntries[], 
                          int mibEntriesCount, int non_repeaters, int max_repetitions) {
    unsigned char varbind_list[BUFFER_SIZE];
    int varbind_list_len = 0;

    char requested_oid_str[BUFFER_SIZE];
    oid_to_string(request_packet->oid, request_packet->oid_len, requested_oid_str);
    printf("requested_oid_str: %s\n", requested_oid_str);

    int start_index = -1;

    // 요청된 OID 이후의 첫 번째 항목을 찾기
    for (int i = 0; i < mibEntriesCount; i++) {
        unsigned char mib_oid[BUFFER_SIZE];
        int mib_oid_len = string_to_oid(mibEntries[i]->oid, mib_oid);
        int cmp_result = oid_compare(request_packet->oid, request_packet->oid_len, mib_oid, mib_oid_len);
        if (cmp_result < 0) {
            start_index = i; // 요청된 OID 이후의 첫 번째 항목 설정
            break;
        } else if (cmp_result == 0) {
            start_index = i + 1;
            break;
        }
    }

    if (start_index == -1) {
        *response_len = 0;
        return;
    }

    int i = start_index;

    for (int j = 0; j < non_repeaters && i < mibEntriesCount; j++, i++) {
        unsigned char varbind[BUFFER_SIZE];
        int varbind_len = 0;

        // OID 인코딩
        unsigned char oid_buffer[BUFFER_SIZE];
        int oid_len = string_to_oid(mibEntries[i]->oid, oid_buffer);

        // Value 인코딩
        unsigned char value_buffer[BUFFER_SIZE];
        int value_len = 0;
        if (mibEntries[i]->value_type == VALUE_TYPE_STRING) {
            value_len = strlen(mibEntries[i]->value.str_value);
            memcpy(value_buffer, mibEntries[i]->value.str_value, value_len);
        } else if (mibEntries[i]->value_type == VALUE_TYPE_INT) {
            int int_value = mibEntries[i]->value.int_value;
            value_len = 4;
            value_buffer[0] = (int_value >> 24) & 0xFF;
            value_buffer[1] = (int_value >> 16) & 0xFF;
            value_buffer[2] = (int_value >> 8) & 0xFF;
            value_buffer[3] = int_value & 0xFF;
        } else if (mibEntries[i]->value_type == VALUE_TYPE_OID) {
            value_len = string_to_oid(mibEntries[i]->value.oid_value, value_buffer);
        } else if (mibEntries[i]->value_type == VALUE_TYPE_TIME_TICKS) {
            unsigned long ticks_value = mibEntries[i]->value.ticks_value;
            value_len = 4;
            value_buffer[0] = (ticks_value >> 24) & 0xFF;
            value_buffer[1] = (ticks_value >> 16) & 0xFF;
            value_buffer[2] = (ticks_value >> 8) & 0xFF;
            value_buffer[3] = ticks_value & 0xFF;
        }

        // Value 필드 작성
        unsigned char value_field[BUFFER_SIZE];
        int value_field_len = 0;
        if (mibEntries[i]->value_type == VALUE_TYPE_STRING) {
            value_field[value_field_len++] = 0x04; // OCTET STRING
        } else if (mibEntries[i]->value_type == VALUE_TYPE_INT) {
            value_field[value_field_len++] = 0x02; // INTEGER
        } else if (mibEntries[i]->value_type == VALUE_TYPE_OID) {
            value_field[value_field_len++] = 0x06; // OBJECT IDENTIFIER
        } else if (mibEntries[i]->value_type == VALUE_TYPE_TIME_TICKS) {
            value_field[value_field_len++] = 0x43; // TimeTicks
        } else {
            value_field[value_field_len++] = 0x05; // NULL
        }
        value_field_len += encode_length(&value_field[value_field_len], value_len);
        memcpy(&value_field[value_field_len], value_buffer, value_len);
        value_field_len += value_len;

        // OID 필드 작성 (OBJECT IDENTIFIER)
        unsigned char oid_field[BUFFER_SIZE];
        int oid_field_len = 0;
        oid_field[oid_field_len++] = 0x06; // OBJECT IDENTIFIER
        oid_field_len += encode_length(&oid_field[oid_field_len], oid_len);
        memcpy(&oid_field[oid_field_len], oid_buffer, oid_len);
        oid_field_len += oid_len;

        // VarBind 작성 (SEQUENCE)
        varbind[varbind_len++] = 0x30; // SEQUENCE
        int varbind_content_len = oid_field_len + value_field_len;
        varbind_len += encode_length(&varbind[varbind_len], varbind_content_len);
        memcpy(&varbind[varbind_len], oid_field, oid_field_len);
        varbind_len += oid_field_len;
        memcpy(&varbind[varbind_len], value_field, value_field_len);
        varbind_len += value_field_len;

        // VarBind를 VarBindList에 추가
        memcpy(&varbind_list[varbind_list_len], varbind, varbind_len);
        varbind_list_len += varbind_len;
    }

    // Max-repetitions 처리: 이후 max_repetitions 횟수만큼 반복
    for (int repetitions = 0; repetitions < max_repetitions; repetitions++) {
        if (i >= mibEntriesCount) {
            // MIB 트리의 끝에 도달했을 경우, endOfMibView 추가
            unsigned char varbind[BUFFER_SIZE];
            int varbind_len = 0;

            // OID 인코딩 (마지막 항목의 OID를 그대로 사용)
            unsigned char oid_buffer[BUFFER_SIZE];
            int oid_len = string_to_oid(mibEntries[mibEntriesCount - 1]->oid, oid_buffer);

            // Value 필드 작성 (endOfMibView)
            unsigned char value_field[BUFFER_SIZE];
            int value_field_len = 0;
            value_field[value_field_len++] = 0x82; // endOfMibView
            value_field_len += encode_length(&value_field[value_field_len], 0);

            // OID 필드 작성 (OBJECT IDENTIFIER)
            unsigned char oid_field[BUFFER_SIZE];
            int oid_field_len = 0;
            oid_field[oid_field_len++] = 0x06; // OBJECT IDENTIFIER
            oid_field_len += encode_length(&oid_field[oid_field_len], oid_len);
            memcpy(&oid_field[oid_field_len], oid_buffer, oid_len);
            oid_field_len += oid_len;

            // VarBind 작성 (SEQUENCE)
            varbind[varbind_len++] = 0x30; // SEQUENCE
            int varbind_content_len = oid_field_len + value_field_len;
            varbind_len += encode_length(&varbind[varbind_len], varbind_content_len);
            memcpy(&varbind[varbind_len], oid_field, oid_field_len);
            varbind_len += oid_field_len;
            memcpy(&varbind[varbind_len], value_field, value_field_len);
            varbind_len += value_field_len;

            // VarBind를 VarBindList에 추가
            memcpy(&varbind_list[varbind_list_len], varbind, varbind_len);
            varbind_list_len += varbind_len;
            break; // endOfMibView가 추가되면 반복을 종료
        }

        // OID와 Value를 VarBind에 추가
        unsigned char varbind[BUFFER_SIZE];
        int varbind_len = 0;

        // OID 인코딩
        unsigned char oid_buffer[BUFFER_SIZE];
        int oid_len = string_to_oid(mibEntries[i]->oid, oid_buffer);

        // Value 인코딩
        unsigned char value_buffer[BUFFER_SIZE];
        int value_len = 0;
        if (mibEntries[i]->value_type == VALUE_TYPE_STRING) {
            value_len = strlen(mibEntries[i]->value.str_value);
            memcpy(value_buffer, mibEntries[i]->value.str_value, value_len);
        } else if (mibEntries[i]->value_type == VALUE_TYPE_INT) {
            int int_value = mibEntries[i]->value.int_value;
            value_len = 4;
            value_buffer[0] = (int_value >> 24) & 0xFF;
            value_buffer[1] = (int_value >> 16) & 0xFF;
            value_buffer[2] = (int_value >> 8) & 0xFF;
            value_buffer[3] = int_value & 0xFF;
        } else if (mibEntries[i]->value_type == VALUE_TYPE_OID) {
            value_len = string_to_oid(mibEntries[i]->value.oid_value, value_buffer);
        } else if (mibEntries[i]->value_type == VALUE_TYPE_TIME_TICKS) {
            unsigned long ticks_value = mibEntries[i]->value.ticks_value;
            value_len = 4;
            value_buffer[0] = (ticks_value >> 24) & 0xFF;
            value_buffer[1] = (ticks_value >> 16) & 0xFF;
            value_buffer[2] = (ticks_value >> 8) & 0xFF;
            value_buffer[3] = ticks_value & 0xFF;
        }

        // Value 필드 작성
        unsigned char value_field[BUFFER_SIZE];
        int value_field_len = 0;
        if (mibEntries[i]->value_type == VALUE_TYPE_STRING) {
            value_field[value_field_len++] = 0x04; // OCTET STRING
        } else if (mibEntries[i]->value_type == VALUE_TYPE_INT) {
            value_field[value_field_len++] = 0x02; // INTEGER
        } else if (mibEntries[i]->value_type == VALUE_TYPE_OID) {
            value_field[value_field_len++] = 0x06; // OBJECT IDENTIFIER
        } else if (mibEntries[i]->value_type == VALUE_TYPE_TIME_TICKS) {
            value_field[value_field_len++] = 0x43; // TimeTicks
        } else {
            value_field[value_field_len++] = 0x05; // NULL
        }
        value_field_len += encode_length(&value_field[value_field_len], value_len);
        memcpy(&value_field[value_field_len], value_buffer, value_len);
        value_field_len += value_len;

        // OID 필드 작성 (OBJECT IDENTIFIER)
        unsigned char oid_field[BUFFER_SIZE];
        int oid_field_len = 0;
        oid_field[oid_field_len++] = 0x06; // OBJECT IDENTIFIER
        oid_field_len += encode_length(&oid_field[oid_field_len], oid_len);
        memcpy(&oid_field[oid_field_len], oid_buffer, oid_len);
        oid_field_len += oid_len;

        // VarBind 작성 (SEQUENCE)
        varbind[varbind_len++] = 0x30; // SEQUENCE
        int varbind_content_len = oid_field_len + value_field_len;
        varbind_len += encode_length(&varbind[varbind_len], varbind_content_len);
        memcpy(&varbind[varbind_len], oid_field, oid_field_len);
        varbind_len += oid_field_len;
        memcpy(&varbind[varbind_len], value_field, value_field_len);
        varbind_len += value_field_len;

        // VarBind를 VarBindList에 추가
        memcpy(&varbind_list[varbind_list_len], varbind, varbind_len);
        varbind_list_len += varbind_len;

        // 다음 반복을 위해 인덱스 증가
        i++;
    }

    // Variable Bindings 작성 (SEQUENCE)
    unsigned char varbind_list_field[BUFFER_SIZE];
    int varbind_list_field_len = 0;
    varbind_list_field[varbind_list_field_len++] = 0x30; // SEQUENCE
    varbind_list_field_len += encode_length(&varbind_list_field[varbind_list_field_len], varbind_list_len);
    memcpy(&varbind_list_field[varbind_list_field_len], varbind_list, varbind_list_len);
    varbind_list_field_len += varbind_list_len;

    // 이후 PDU 작성 및 응답 생성
    unsigned char pdu[BUFFER_SIZE];
    int pdu_len = 0;
    pdu[pdu_len++] = 0xA2; // GET-RESPONSE PDU
    unsigned char pdu_content[BUFFER_SIZE];
    int pdu_content_len = 0;

    // Request ID
    pdu_content[pdu_content_len++] = 0x02; // INTEGER
    pdu_content[pdu_content_len++] = 0x04; // 길이 4바이트
    pdu_content[pdu_content_len++] = (request_packet->request_id >> 24) & 0xFF;
    pdu_content[pdu_content_len++] = (request_packet->request_id >> 16) & 0xFF;
    pdu_content[pdu_content_len++] = (request_packet->request_id >> 8) & 0xFF;
    pdu_content[pdu_content_len++] = request_packet->request_id & 0xFF;

    // Error Status
    pdu_content[pdu_content_len++] = 0x02; // INTEGER
    pdu_content[pdu_content_len++] = 0x01; // 길이 1바이트
    pdu_content[pdu_content_len++] = 0x00; // noError

    // Error Index
    pdu_content[pdu_content_len++] = 0x02; // INTEGER
    pdu_content[pdu_content_len++] = 0x01; // 길이 1바이트
    pdu_content[pdu_content_len++] = 0x00; // noError

    // Variable Bindings 추가
    memcpy(&pdu_content[pdu_content_len], varbind_list_field, varbind_list_field_len);
    pdu_content_len += varbind_list_field_len;

    // PDU 길이 설정
    pdu_len += encode_length(&pdu[pdu_len], pdu_content_len);
    memcpy(&pdu[pdu_len], pdu_content, pdu_content_len);
    pdu_len += pdu_content_len;

    // 전체 메시지 작성 (SEQUENCE)
    int cursor = 0;
    response[cursor++] = 0x30; // SEQUENCE

    // 메시지 내용 작성
    unsigned char message_content[BUFFER_SIZE];
    int message_content_len = 0;

    // SNMP 버전
    message_content[message_content_len++] = 0x02; // INTEGER
    message_content[message_content_len++] = 0x01; // 길이 1바이트
    message_content[message_content_len++] = request_packet->version;

    // 커뮤니티 문자열
    message_content[message_content_len++] = 0x04; // OCTET STRING
    int community_len = strlen(request_packet->community);
    message_content_len += encode_length(&message_content[message_content_len], community_len);
    memcpy(&message_content[message_content_len], request_packet->community, community_len);
    message_content_len += community_len;

    // PDU 추가
    memcpy(&message_content[message_content_len], pdu, pdu_len);
    message_content_len += pdu_len;

    // 전체 메시지 길이 설정
    cursor += encode_length(&response[cursor], message_content_len);
    memcpy(&response[cursor], message_content, message_content_len);
    cursor += message_content_len;

    // 응답 길이 설정
    *response_len = cursor;
}

// SNMP 응답 생성
void create_snmp_response(SNMPPacket *request_packet, unsigned char *response, int *response_len,
                          unsigned char *response_oid, int response_oid_len, MIBNode *entry,
                          int error_status, int error_index, int snmp_version)
{
    int index = 0;

    // 메시지 전체를 임시 버퍼에 작성
    unsigned char buffer[BUFFER_SIZE];

    // 1. SNMP Version
    buffer[index++] = 0x02; // INTEGER
    index += encode_length(&buffer[index], 1);
    buffer[index++] = request_packet->version;

    // 2. Community String
    buffer[index++] = 0x04; // OCTET STRING
    int community_length = strlen(request_packet->community);
    index += encode_length(&buffer[index], community_length);
    memcpy(&buffer[index], request_packet->community, community_length);
    index += community_length;

    // 3. PDU
    buffer[index++] = 0xA2; // GET-RESPONSE PDU
    int pdu_length_pos = index++; // PDU 길이 위치를 저장

    // 3.1. Request ID
    buffer[index++] = 0x02; // INTEGER
    index += encode_length(&buffer[index], 4);
    buffer[index++] = (request_packet->request_id >> 24) & 0xFF;
    buffer[index++] = (request_packet->request_id >> 16) & 0xFF;
    buffer[index++] = (request_packet->request_id >> 8) & 0xFF;
    buffer[index++] = request_packet->request_id & 0xFF;

    // 3.2. Error Status
    buffer[index++] = 0x02; // INTEGER
    index += encode_length(&buffer[index], 1);

    // SNMPv1과 SNMPv2c의 에러 상태 처리
    if (snmp_version == 1) {
        // SNMPv1은 에러 코드 사용
        buffer[index++] = error_status;
    } else {
        if (error_status >= SNMP_EXCEPTION_NO_SUCH_OBJECT && error_status <= SNMP_EXCEPTION_END_OF_MIB_VIEW) {
            buffer[index++] = 0;
        } else {
            buffer[index++] = (error_status == SNMP_ERROR_NO_ERROR) ? 0 : error_status;
        }
    }

    // 3.3. Error Index
    buffer[index++] = 0x02; // INTEGER
    index += encode_length(&buffer[index], 1);
    buffer[index++] = error_index;

    // 3.4. Variable Bindings
    buffer[index++] = 0x30; // SEQUENCE
    int varbind_list_length_pos = index++; // Variable Bindings 길이 위치 저장

    // 3.4.1. Variable Binding
    buffer[index++] = 0x30; // SEQUENCE
    int varbind_length_pos = index++; // Variable Binding 길이 위치 저장

    // 3.4.1.1. OID
    buffer[index++] = 0x06; // OBJECT IDENTIFIER
    index += encode_length(&buffer[index], response_oid_len);
    memcpy(&buffer[index], response_oid, response_oid_len);
    index += response_oid_len;

    // 3.4.1.2. Value
    if (error_status == SNMP_ERROR_NO_ERROR) {
        if (entry->value_type == VALUE_TYPE_INT) {
            buffer[index++] = 0x02; // INTEGER
            unsigned long value = entry->value.int_value;
            int value_len = (value <= 0xFF) ? 1 : (value <= 0xFFFF) ? 2 : (value <= 0xFFFFFF) ? 3 : 4;
            index += encode_length(&buffer[index], value_len);
            for (int i = value_len - 1; i >= 0; i--) {
                buffer[index++] = (value >> (i * 8)) & 0xFF;
            }
        } else if (entry->value_type == VALUE_TYPE_STRING) {
            buffer[index++] = 0x04; // OCTET STRING
            int value_length = strlen(entry->value.str_value);
            index += encode_length(&buffer[index], value_length);
            memcpy(&buffer[index], entry->value.str_value, value_length);
            index += value_length;
        } else if (entry->value_type == VALUE_TYPE_OID) {
            buffer[index++] = 0x06; // OBJECT IDENTIFIER
            int oid_length = string_to_oid(entry->value.oid_value, &buffer[index + 1]);
            index += encode_length(&buffer[index], oid_length); // OID 길이를 인코딩
            index += oid_length;
        } else if (entry->value_type == VALUE_TYPE_TIME_TICKS) {
            buffer[index++] = 0x43; // TimeTicks (SNMPv2)
            unsigned long ticks_value = entry->value.ticks_value;
            int value_len = (ticks_value <= 0xFF) ? 1 : (ticks_value <= 0xFFFF) ? 2 : (ticks_value <= 0xFFFFFF) ? 3 : 4;
            index += encode_length(&buffer[index], value_len);
            for (int i = value_len - 1; i >= 0; i--) {
                buffer[index++] = (ticks_value >> (i * 8)) & 0xFF;
            }
        } else {
            buffer[index++] = 0x05; // NULL
            index += encode_length(&buffer[index], 0);
        }
    } else {
        if (snmp_version == 1) {
            buffer[index++] = 0x05; // NULL
            index += encode_length(&buffer[index], 0);
        } else if (snmp_version == 2) {
            switch (error_status) {
                case SNMP_EXCEPTION_NO_SUCH_OBJECT:
                    buffer[index++] = 0x80; // noSuchObject
                    break;
                case SNMP_EXCEPTION_NO_SUCH_INSTANCE:
                    buffer[index++] = 0x81; // noSuchInstance
                    break;
                case SNMP_EXCEPTION_END_OF_MIB_VIEW:
                    buffer[index++] = 0x82; // endOfMibView
                    break;
                default:
                    buffer[index++] = 0x80; // 기본적으로 noSuchObject로 설정
            }
            index += encode_length(&buffer[index], 0);
        }
    }

    // Variable Binding 길이 설정
    int varbind_length = index - varbind_length_pos - 1;
    int varbind_length_bytes = encode_length(&buffer[varbind_length_pos], varbind_length);
    memmove(&buffer[varbind_length_pos + varbind_length_bytes],
            &buffer[varbind_length_pos + 1],
            index - (varbind_length_pos + 1));
    index += (varbind_length_bytes - 1);

    // Variable Bindings 길이 설정
    int varbind_list_length = index - varbind_list_length_pos - 1;
    int varbind_list_length_bytes = encode_length(&buffer[varbind_list_length_pos], varbind_list_length);
    memmove(&buffer[varbind_list_length_pos + varbind_list_length_bytes],
            &buffer[varbind_list_length_pos + 1],
            index - (varbind_list_length_pos + 1));
    index += (varbind_list_length_bytes - 1);

    // PDU 길이 설정
    int pdu_length = index - pdu_length_pos - 1;
    int pdu_length_bytes = encode_length(&buffer[pdu_length_pos], pdu_length);
    memmove(&buffer[pdu_length_pos + pdu_length_bytes],
            &buffer[pdu_length_pos + 1],
            index - (pdu_length_pos + 1));
    index += (pdu_length_bytes - 1);

    // 전체 메시지를 SEQUENCE로 감싸기
    unsigned char final_buffer[BUFFER_SIZE];
    int final_index = 0;
    final_buffer[final_index++] = 0x30; // SEQUENCE
    int message_length = index;
    final_index += encode_length(&final_buffer[final_index], message_length);
    memcpy(&final_buffer[final_index], buffer, index);
    final_index += index;

    // 최종 응답 설정
    memcpy(response, final_buffer, final_index);
    *response_len = final_index;
}

// SNMPv3 응답 생성
void create_snmpv3_response(SNMPv3Packet *request_packet, unsigned char *response, int *response_len,
                            unsigned char *response_oid, int response_oid_len, MIBNode *entry,
                            int error_status, int error_index) {
    int index = 0;
    unsigned char buffer[BUFFER_SIZE];

    // 1. SNMP Version (SNMPv3)
    buffer[index++] = 0x02; // INTEGER
    index += encode_length(&buffer[index], 1);
    buffer[index++] = 3; // Version 3

    // 2. msgGlobalData SEQUENCE
    buffer[index++] = 0x30; // SEQUENCE
    int global_data_length_pos = index++; // Length placeholder

    // 2.1 msgID
    buffer[index++] = 0x02; // INTEGER
    index += encode_length(&buffer[index], 4);
    buffer[index++] = (request_packet->msgID >> 24) & 0xFF;
    buffer[index++] = (request_packet->msgID >> 16) & 0xFF;
    buffer[index++] = (request_packet->msgID >> 8) & 0xFF;
    buffer[index++] = request_packet->msgID & 0xFF;

    // 2.2 msgMaxSize
    buffer[index++] = 0x02; // INTEGER
    index += encode_length(&buffer[index], 4);
    buffer[index++] = (request_packet->msgMaxSize >> 24) & 0xFF;
    buffer[index++] = (request_packet->msgMaxSize >> 16) & 0xFF;
    buffer[index++] = (request_packet->msgMaxSize >> 8) & 0xFF;
    buffer[index++] = request_packet->msgMaxSize & 0xFF;

    // 2.3 msgFlags
    buffer[index++] = 0x04; // OCTET STRING
    index += encode_length(&buffer[index], 1);
    buffer[index++] = request_packet->msgFlags[0];

    // 2.4 msgSecurityModel
    buffer[index++] = 0x02; // INTEGER
    index += encode_length(&buffer[index], 1);
    buffer[index++] = request_packet->msgSecurityModel;

    // Calculate Global Data Length
    int global_data_length = index - global_data_length_pos - 1;
    encode_length_at(&buffer[global_data_length_pos], global_data_length);

    // 3. Security Parameters (OCTET STRING)
    buffer[index++] = 0x04; // OCTET STRING
    int sec_params_length_pos = index++; // Length placeholder

    // Security Parameters SEQUENCE
    buffer[index++] = 0x30; // SEQUENCE
    int usm_length_pos = index++; // Length placeholder

    // 3.1 msgAuthoritativeEngineID
    buffer[index++] = 0x04; // OCTET STRING
    index += encode_length(&buffer[index], request_packet->msgAuthoritativeEngineID_len);
    memcpy(&buffer[index], request_packet->msgAuthoritativeEngineID, request_packet->msgAuthoritativeEngineID_len);
    index += request_packet->msgAuthoritativeEngineID_len;

    // 3.2 msgAuthoritativeEngineBoots
    buffer[index++] = 0x02; // INTEGER
    unsigned char boots_buf[5];
    int boots_len = encode_integer(request_packet->msgAuthoritativeEngineBoots, boots_buf);
    index += encode_length(&buffer[index], boots_len);
    memcpy(&buffer[index], boots_buf, boots_len);
    index += boots_len;

    // 3.3 msgAuthoritativeEngineTime
    buffer[index++] = 0x02; // INTEGER
    unsigned char time_buf[5];
    int time_len = encode_integer(request_packet->msgAuthoritativeEngineTime, time_buf);
    index += encode_length(&buffer[index], time_len);
    memcpy(&buffer[index], time_buf, time_len);
    index += time_len;


    // 3.4 msgUserName
    buffer[index++] = 0x04; // OCTET STRING
    int user_name_len = strlen(request_packet->msgUserName);
    index += encode_length(&buffer[index], user_name_len);
    memcpy(&buffer[index], request_packet->msgUserName, user_name_len);
    index += user_name_len;

    // 3.5 Authentication Parameters
    buffer[index++] = 0x04; // OCTET STRING
    index += encode_length(&buffer[index], request_packet->msgAuthenticationParameters_len);
    memcpy(&buffer[index], request_packet->msgAuthenticationParameters, request_packet->msgAuthenticationParameters_len);
    index += request_packet->msgAuthenticationParameters_len;

    // 3.6 Privacy Parameters
    buffer[index++] = 0x04; // OCTET STRING
    index += encode_length(&buffer[index], request_packet->msgPrivacyParameters_len);
    memcpy(&buffer[index], request_packet->msgPrivacyParameters, request_packet->msgPrivacyParameters_len);
    index += request_packet->msgPrivacyParameters_len;

    // Update USM length
    int usm_length = index - usm_length_pos - 1;
    encode_length_at(&buffer[usm_length_pos], usm_length);

    // Encode Security Parameters Length
    int sec_params_length = index - sec_params_length_pos - 1;
    encode_length_at(&buffer[sec_params_length_pos], sec_params_length);

    // 4. Scoped PDU
    buffer[index++] = 0x30; // SEQUENCE
    int scoped_pdu_length_pos = index++; // Length placeholder

    // 4.1 contextEngineID
    buffer[index++] = 0x04; // OCTET STRING
    index += encode_length(&buffer[index], request_packet->contextEngineID_len);
    memcpy(&buffer[index], request_packet->contextEngineID, request_packet->contextEngineID_len);
    index += request_packet->contextEngineID_len;

    // 4.2 contextName
    buffer[index++] = 0x04; // OCTET STRING
    int context_name_len = strlen(request_packet->contextName);
    index += encode_length(&buffer[index], context_name_len);
    memcpy(&buffer[index], request_packet->contextName, context_name_len);
    index += context_name_len;

    // 4.3 PDU SEQUENCE (Response PDU)
    buffer[index++] = 0xA2; // Response PDU (응답 PDU 타입)
    int pdu_length_pos = index++; // PDU length placeholder

    // 4.3.1 Request ID
    buffer[index++] = 0x02; // INTEGER
    index += encode_length(&buffer[index], 4);
    buffer[index++] = (request_packet->request_id >> 24) & 0xFF;
    buffer[index++] = (request_packet->request_id >> 16) & 0xFF;
    buffer[index++] = (request_packet->request_id >> 8) & 0xFF;
    buffer[index++] = request_packet->request_id & 0xFF;

    // 4.3.2 Error Status
    buffer[index++] = 0x02; // INTEGER
    index += encode_length(&buffer[index], 1);


    // 예외 상태일 때 error_status를 0으로 설정
    if (error_status == SNMP_EXCEPTION_NO_SUCH_OBJECT ||
        error_status == SNMP_EXCEPTION_NO_SUCH_INSTANCE ||
        error_status == SNMP_EXCEPTION_END_OF_MIB_VIEW) {
        buffer[index++] = 0;
    } else {
        buffer[index++] = error_status;
    }

    // 4.3.3 Error Index
    buffer[index++] = 0x02; // INTEGER
    index += encode_length(&buffer[index], 1);
    buffer[index++] = error_index;

    // 4.3.4 VarBind List
    buffer[index++] = 0x30; // SEQUENCE for VarBind list
    int varbind_list_length_pos = index++; // Length placeholder

    // 4.3.4.1 VarBind
    buffer[index++] = 0x30; // SEQUENCE for VarBind
    int varbind_length_pos = index++; // Length placeholder

    // 4.3.4.1.1 OID
    buffer[index++] = 0x06; // OBJECT IDENTIFIER
    index += encode_length(&buffer[index], response_oid_len);
    memcpy(&buffer[index], response_oid, response_oid_len);
    index += response_oid_len;

    // 4.3.4.1.2 Value (according to MIB entry type)
    if (entry) {
        switch(entry->value_type) {
            case VALUE_TYPE_INT:
                buffer[index++] = 0x02; // INTEGER 태그

                // INTEGER 값 인코딩
                unsigned char int_encoded[BUFFER_SIZE];
                int int_encoded_len = encode_integer(entry->value.int_value, int_encoded);

                // 길이(Byte Length) 인코딩
                index += encode_length(&buffer[index], int_encoded_len);

                // 인코딩된 INTEGER 값을 버퍼에 복사
                memcpy(&buffer[index], int_encoded, int_encoded_len);
                index += int_encoded_len;
                break;
            
            case VALUE_TYPE_STRING:
                buffer[index++] = 0x04; // OCTET STRING
                {
                    int str_len = strlen(entry->value.str_value);
                    printf("Debug: STRING Value = %s\n", entry->value.str_value); // 디버깅 출력
                    index += encode_length(&buffer[index], str_len);
                    memcpy(&buffer[index], entry->value.str_value, str_len);
                    index += str_len;
                }
                break;
            
            case VALUE_TYPE_OID:
                buffer[index++] = 0x06; // OBJECT IDENTIFIER
                {
                    int oid_len = string_to_oid(entry->value.oid_value, &buffer[index + 1]);
                    printf("Debug: OID Value = %s (Length: %d)\n", entry->value.oid_value, oid_len); // 디버깅 출력
                    buffer[index++] = oid_len;
                    index += oid_len;
                }
                break;
            
            case VALUE_TYPE_TIME_TICKS:
                buffer[index++] = 0x43; // TimeTicks (APPLICATION 3)
                {
                    unsigned long ticks = entry->value.ticks_value;
                    printf("Debug: TimeTicks Value = %lu\n", ticks); // 디버깅 출력
                    int ticks_len = (ticks <= 0xFF) ? 1 :
                                    (ticks <= 0xFFFF) ? 2 :
                                    (ticks <= 0xFFFFFF) ? 3 : 4;
                    index += encode_length(&buffer[index], ticks_len);
                    for(int i = ticks_len - 1; i >=0; i--){
                        buffer[index++] = (ticks >> (i*8)) & 0xFF;
                    }
                }
                break;
            
            default:
                buffer[index++] = 0x05; // NULL
                index += encode_length(&buffer[index], 0);
                // printf("Debug: Unsupported Value Type. Encoded as NULL.\n");
                break;
        }
    } else {
        buffer[index++] = error_status; // noSuchObject for SNMPv3
        index += encode_length(&buffer[index], 0);
        // printf("Debug: entry is NULL. Encoded as noSuchObject.\n");s
    }

    // Update VarBind Length
    int varbind_length = index - varbind_length_pos - 1;
    encode_length_at(&buffer[varbind_length_pos], varbind_length);

    // Update VarBind List Length
    int varbind_list_length = index - varbind_list_length_pos - 1;
    encode_length_at(&buffer[varbind_list_length_pos], varbind_list_length);

    // Update PDU Length
    int pdu_length = index - pdu_length_pos - 1;
    encode_length_at(&buffer[pdu_length_pos], pdu_length);

    // Update Scoped PDU Length
    int scoped_pdu_length = index - scoped_pdu_length_pos - 1;
    encode_length_at(&buffer[scoped_pdu_length_pos], scoped_pdu_length);

    // Final wrapping with SEQUENCE
    unsigned char final_buffer[BUFFER_SIZE];
    int final_index = 0;
    final_buffer[final_index++] = 0x30; // SEQUENCE tag for the entire SNMP message

    // Encode the length of the entire message
    int message_length = index;
    final_index += encode_length(&final_buffer[final_index], message_length);
    memcpy(&final_buffer[final_index], buffer, index);
    final_index += index;

    memcpy(response, final_buffer, final_index);
    *response_len = final_index;
}

void update_dynamic_values() {
    for (int i = 0; i < node_count; i++) {
        if (strcmp(nodes[i]->name, "sysUpTime") == 0) {
            nodes[i]->value.ticks_value = get_system_uptime();
        } else if (strcmp(nodes[i]->name, "dateTimeInfo") == 0) {
            strcpy(nodes[i]->value.str_value, get_date());
        } else if (strcmp(nodes[i]->name, "cpuUsage") == 0) {
            nodes[i]->value.int_value = get_cpuUsage();
        } else if (strcmp(nodes[i]->name, "memoryusage") == 0) {
            nodes[i]->value.int_value = get_memory_usage();
        }
    }
}

void print_snmp_packet(SNMPPacket *snmp_packet) {
    printf("SNMP Version: %s\n", snmp_version(snmp_packet->version));
    printf("Community: %s\n", snmp_packet->community);
    printf("PDU Type: %s\n", pdu_type_str(snmp_packet->pdu_type));
    printf("Request ID: %u\n", snmp_packet->request_id);
    printf("Error Status: %d\n", snmp_packet->error_status);
    printf("Error Index: %d\n", snmp_packet->error_index);

    if (snmp_packet->pdu_type == 0xA5){
        printf("Error non_repeaters: %d\n", snmp_packet->non_repeaters);
        printf("Error max_repetitions: %d\n", snmp_packet->max_repetitions);
    }
    printf("OID: ");
    for (int i = 0; i < snmp_packet->oid_len; i++) {
        printf("%02X ", snmp_packet->oid[i]);
    }
    printf("\n");
}

void snmp_request(unsigned char *buffer, int n, struct sockaddr_in *cliaddr, int sockfd, int snmp_version, const char *allowed_community) {
    update_dynamic_values();

    if (snmp_version == 3) {
        SNMPv3Packet snmp_packet;
        memset(&snmp_packet, 0, sizeof(SNMPv3Packet));

        int index = 0;
        parse_snmpv3_message(buffer, &index, n, &snmp_packet);

        // // 파싱 결과 출력
        // printf("SNMP Version: %d\n", snmp_packet.version);
        // printf("msgID: %d\n", snmp_packet.msgID);
        // printf("msgAuthoritativeEngineID: ");
        // for (int i = 0; i < 32; i++) {
        //     printf("%02X ", snmp_packet.msgAuthoritativeEngineID[i]);
        // }
        // printf("\n");
        // printf("msgMaxSize: %d\n", snmp_packet.msgMaxSize);
        // printf("msgFlags: %02X\n", snmp_packet.msgFlags[0]);
        // printf("msgSecurityModel: %d\n", snmp_packet.msgSecurityModel);
        // printf("msgUserName: %s\n", snmp_packet.msgUserName);
        printf("PDU Type: %02X\n", snmp_packet.pdu_type);
        // printf("Request ID: %d\n", snmp_packet.request_id);
        // printf("Error Status: %d\n", snmp_packet.error_status);
        // printf("Error Index: %d\n", snmp_packet.error_index);
        // printf("VarBind Count: %d\n", snmp_packet.varbind_count);

        // for (int i = 0; i < snmp_packet.varbind_count; i++) {
        //     printf("VarBind %d:\n", i + 1);
        //     printf("  OID: ");
        //     for (int j = 0; j < snmp_packet.varbind_list[i].oid_len; j++) {
        //         printf("%02X ", snmp_packet.varbind_list[i].oid[j]);
        //     }
        //     printf("\n");
        //     printf("  Value Type: %02X\n", snmp_packet.varbind_list[i].value_type);
        //     printf("  Value Length: %d\n", snmp_packet.varbind_list[i].value_len);
        // }

        if (snmp_packet.msgAuthoritativeEngineID_len == 0) {
            unsigned char response[BUFFER_SIZE];
            int response_len = 0;

            // 보고서 응답 생성
            create_snmpv3_report_response(&snmp_packet, response, &response_len, SNMPERR_USM_UNKNOWNENGINEID);

            // 응답 전송
            if (response_len > 0) {
                sendto(sockfd, response, response_len, 0, (struct sockaddr *)cliaddr, sizeof(*cliaddr));
            }
            return;
        }

        // 요청된 OID를 문자열로 변환
        char requested_oid_str[BUFFER_SIZE];;
        oid_to_string(snmp_packet.varbind_list[0].oid, snmp_packet.varbind_list[0].oid_len, requested_oid_str);

        unsigned char response[BUFFER_SIZE];
        int response_len = 0;        

        // MIB에서 해당 OID를 검색
        MIBNode *entry = NULL;
        for (int i = 0; i < node_count; i++) {
            if (strcmp(nodes[i]->oid, requested_oid_str) == 0) {
                entry = nodes[i];
                // printf("VarBind %d OID: %s\n", i + 1, requested_oid_str);
                break;
            }
        }

        // PDU 타입에 따라 처리
        switch (snmp_packet.pdu_type) {
            case 0xA0: // GetRequest
                if (entry != NULL) {
                    // MIB 항목을 찾았을 때 정상적인 응답 생성
                    create_snmpv3_response(&snmp_packet, response, &response_len, 
                               snmp_packet.varbind_list[0].oid, snmp_packet.varbind_list[0].oid_len, 
                               entry, SNMP_ERROR_NO_ERROR, 0);
                    printf("GetRequest 처리 완료\n");
                } else {
                    // MIB 항목을 찾지 못했을 때 오류 응답 생성 (noSuchObject)
                    create_snmpv3_response(&snmp_packet, response, &response_len, 
                               snmp_packet.varbind_list[0].oid, snmp_packet.varbind_list[0].oid_len, 
                               NULL, SNMP_EXCEPTION_NO_SUCH_OBJECT, 0);
                    printf("GetRequest: noSuchObject 오류 응답 생성\n");
                }
                break;

            case 0xA1: // GetNextRequest
                {
                    MIBNode *nextEntry = NULL;
                    int found = find_next_mib_entry(snmp_packet.varbind_list[0].oid, snmp_packet.varbind_list[0].oid_len, &nextEntry);
                    
                    if (found && nextEntry != NULL) {
                        // 다음 OID을 바이너리 형식으로 변환
                        unsigned char next_oid_binary[BUFFER_SIZE];
                        int next_oid_binary_len = string_to_oid(nextEntry->oid, next_oid_binary);
                        
                        // 응답에 다음 OID를 포함하여 생성
                        create_snmpv3_response(&snmp_packet, response, &response_len, 
                                            next_oid_binary, next_oid_binary_len, 
                                            nextEntry, SNMP_ERROR_NO_ERROR, 0);
                        printf("GetNextRequest 처리 완료: 다음 OID = %s\n", nextEntry->oid);
                    } else {
                        // 더 이상 OID가 없을 때 오류 응답 생성 (endOfMibView)
                        create_snmpv3_response(&snmp_packet, response, &response_len, 
                                            snmp_packet.varbind_list[0].oid, snmp_packet.varbind_list[0].oid_len, 
                                            NULL, SNMP_EXCEPTION_END_OF_MIB_VIEW, 0);
                        printf("GetNextRequest: endOfMibView 오류 응답 생성\n");
                    }
                }
                break;


            default:
                // 지원하지 않는 PDU 타입에 대한 오류 처리
                printf("지원하지 않는 PDU Type for SNMPv3: %02X\n", snmp_packet.pdu_type);
                create_snmpv3_report_response(&snmp_packet, response, &response_len, SNMP_ERROR_GENERAL_ERROR);
                break;
        }

        // 응답 전송
        if (response_len > 0) {
            sendto(sockfd, response, response_len, 0, (struct sockaddr *)cliaddr, sizeof(*cliaddr));
        }

        return;
    }

    SNMPPacket snmp_packet;
    unsigned char response[BUFFER_SIZE];
    int response_len = 0;

    memset(&snmp_packet, 0, sizeof(SNMPPacket));
    snmp_packet.version = -1;

    int index = 0;
    parse_tlv(buffer, &index, n, &snmp_packet);

    // print_snmp_packet(&snmp_packet);

    if (strcmp(snmp_packet.community, allowed_community) != 0) {
        printf("Unauthorized community: %s\n", snmp_packet.community);
        return;
    }

    char requested_oid_str[BUFFER_SIZE];
    oid_to_string(snmp_packet.oid, snmp_packet.oid_len, requested_oid_str);

    MIBNode *entry = NULL;
    int found = 0;
    int error_status = SNMP_ERROR_NO_ERROR;

    switch (snmp_version) {
        case 1: // SNMPv1 
            if (snmp_packet.pdu_type == 0xA0) { // GET-REQUEST
                for (int i = 0; i < node_count; i++) {
                    if (strcmp(nodes[i]->oid, requested_oid_str) == 0) {
                        entry = nodes[i];
                        found = 1;
                        // printf("VarBind %d OID: %s\n", i + 1, requested_oid_str);
                        break;
                    }
                }
                if (found) {
                    unsigned char response_oid[BUFFER_SIZE];
                    int response_oid_len = string_to_oid(entry->oid, response_oid);

                    create_snmp_response(&snmp_packet, response, &response_len,
                                         response_oid, response_oid_len, entry, error_status, 0, snmp_version);

                    if (response_len > MAX_SNMP_PACKET_SIZE) {
                        error_status = SNMP_ERROR_TOO_BIG;
                        response_len = 0;
                        create_snmp_response(&snmp_packet, response, &response_len,
                                             snmp_packet.oid, snmp_packet.oid_len, NULL, error_status, 0, snmp_version);
                    }
                } else {
                    error_status = SNMP_ERROR_NO_SUCH_NAME;
                    create_snmp_response(&snmp_packet, response, &response_len,
                                         snmp_packet.oid, snmp_packet.oid_len, NULL, error_status, 1, snmp_version);
                }
            } else if (snmp_packet.pdu_type == 0xA1) { // GET-NEXT
                found = find_next_mib_entry(snmp_packet.oid, snmp_packet.oid_len, &entry);

                if (found) {
                    unsigned char response_oid[BUFFER_SIZE];
                    int response_oid_len = string_to_oid(entry->oid, response_oid);

                    create_snmp_response(&snmp_packet, response, &response_len,
                                         response_oid, response_oid_len, entry, error_status, 0, snmp_version);

                    if (response_len > MAX_SNMP_PACKET_SIZE) {
                        error_status = SNMP_ERROR_TOO_BIG;
                        response_len = 0;
                        create_snmp_response(&snmp_packet, response, &response_len,
                                             snmp_packet.oid, snmp_packet.oid_len, NULL, error_status, 0, snmp_version);
                    }
                } else {
                    error_status = SNMP_ERROR_NO_SUCH_NAME;
                    create_snmp_response(&snmp_packet, response, &response_len,
                                         snmp_packet.oid, snmp_packet.oid_len, NULL, error_status, 1, snmp_version);
                }
            } else {
                printf("Unsupported PDU Type for SNMPv1: %d\n", snmp_packet.pdu_type);
                error_status = SNMP_ERROR_GENERAL_ERROR;
                create_snmp_response(&snmp_packet, response, &response_len,
                                     snmp_packet.oid, snmp_packet.oid_len, NULL, error_status, 1, snmp_version);
            }
            break;

        case 2: // SNMPv2c
            if (snmp_packet.pdu_type == 0xA0) { // GET-REQUEST
                for (int i = 0; i < node_count; i++) {
                    if (strcmp(nodes[i]->oid, requested_oid_str) == 0) {
                        entry = nodes[i];
                        found = 1;
                        break;
                    }
                }
                if (found) {
                    unsigned char response_oid[BUFFER_SIZE];
                    int response_oid_len = string_to_oid(entry->oid, response_oid);

                    create_snmp_response(&snmp_packet, response, &response_len,
                                         response_oid, response_oid_len, entry, SNMP_ERROR_NO_ERROR, 0, snmp_version);

                    if (response_len > MAX_SNMP_PACKET_SIZE) {
                        error_status = SNMP_ERROR_TOO_BIG;
                        response_len = 0;
                        create_snmp_response(&snmp_packet, response, &response_len,
                                             snmp_packet.oid, snmp_packet.oid_len, NULL, error_status, 0, snmp_version);
                    }
                } else {
                    error_status = SNMP_EXCEPTION_NO_SUCH_OBJECT;
                    create_snmp_response(&snmp_packet, response, &response_len,
                                         snmp_packet.oid, snmp_packet.oid_len, NULL, error_status, 1, snmp_version);
                }
            } else if (snmp_packet.pdu_type == 0xA1) { // GET-NEXT
                found = find_next_mib_entry(snmp_packet.oid, snmp_packet.oid_len, &entry);
                
                if (found) {
                    unsigned char response_oid[BUFFER_SIZE];
                    int response_oid_len = string_to_oid(entry->oid, response_oid);

                    create_snmp_response(&snmp_packet, response, &response_len,
                                         response_oid, response_oid_len, entry, SNMP_ERROR_NO_ERROR, 0, snmp_version);

                    if (response_len > MAX_SNMP_PACKET_SIZE) {
                        error_status = SNMP_ERROR_TOO_BIG;
                        response_len = 0;
                        create_snmp_response(&snmp_packet, response, &response_len,
                                             snmp_packet.oid, snmp_packet.oid_len, NULL, error_status, 0, snmp_version);
                    }
                } else {
                    error_status = SNMP_EXCEPTION_END_OF_MIB_VIEW;
                    create_snmp_response(&snmp_packet, response, &response_len,
                                         snmp_packet.oid, snmp_packet.oid_len, NULL, error_status, 0, snmp_version);
                }
            } else if (snmp_packet.pdu_type == 0xA5) { // GET-BULK
                printf("Bulk request received\n");

                int non_repeaters = snmp_packet.non_repeaters;
                int max_repetitions = snmp_packet.max_repetitions;

                create_bulk_response(&snmp_packet, response, &response_len, nodes, node_count, non_repeaters, max_repetitions);

                if (response_len > MAX_SNMP_PACKET_SIZE) {
                    int error_status = SNMP_ERROR_TOO_BIG;
                    response_len = 0;
                    create_snmp_response(&snmp_packet, response, &response_len,
                                        snmp_packet.oid, snmp_packet.oid_len, NULL, error_status, 0, 2);
                }
            } else {
                printf("Unsupported PDU Type for SNMPv2c: %d\n", snmp_packet.pdu_type);
                error_status = SNMP_EXCEPTION_END_OF_MIB_VIEW;
                create_snmp_response(&snmp_packet, response, &response_len,
                                     snmp_packet.oid, snmp_packet.oid_len, NULL, error_status, 1, snmp_version);
            }
            break;

        default:
            printf("Unsupported SNMP Version: %d\n", snmp_version);
            return;
    }

    if (response_len > 0) {
        sendto(sockfd, response, response_len, 0, (struct sockaddr *)cliaddr, sizeof(*cliaddr));
    }
}

int update_mib_node_value(const char *name, const void *value) {
    MIBNode *node = NULL;

    for (int i = 0; i < node_count; i++) {
        // printf("Searching node %d: Name='%s'\n", i, nodes[i]->name);
        if (strcmp(nodes[i]->name, name) == 0) {
            node = nodes[i];
            break;
        }
    }

    if (!node) {
        printf("Error: Node %s not found.\n", name);
        return -1;
    }

    if (node->value_type == VALUE_TYPE_INT) {
        node->value.int_value = *(int *)value;
    } else if (node->value_type == VALUE_TYPE_STRING) {
        // printf("Updating node '%s' with value: %s\n", name, (char *)value);
        strncpy(node->value.str_value, (const char *)value, sizeof(node->value.str_value) - 1);
        node->value.str_value[sizeof(node->value.str_value) - 1] = '\0';
    } else if (node->value_type == VALUE_TYPE_TIME_TICKS) {
        node->value.ticks_value = *(unsigned long *)value;
    } else {
        printf("Error: Unsupported value type for node %s.\n", name);
        return -1;
    }

    // printf("Node %s updated successfully with new value.\n", name);
    return 0;
}

void free_mib_nodes() {
    for (int i = 0; i < node_count; i++) {
        free(nodes[i]);
    }
    node_count = 0;

    printf("free_mib_nodes\n");
}

int main(int argc, char *argv[]) {
    int sockfd;
    struct sockaddr_in servaddr, cliaddr;
    unsigned char buffer[BUFFER_SIZE];
    
    // Set default community name(public)
    const char *allowed_community = "public";

    // default snmp version
    int snmp_version = 1;

    // SNMPv3 authentication parameters
    const char *authProtocol = NULL;
    const char *authPassword = NULL;
    const char *privProtocol = NULL;
    const char *privPassword = NULL;

    // SNMPv3 security level
    const char *security_level = "noAuthNoPriv";

    if (argc > 1) {
        if (strcmp(argv[1], "1") == 0) {
            snmp_version = 1;
        } else if (strcmp(argv[1], "2c") == 0) {
            snmp_version = 2;
        } else if (strcmp(argv[1], "3") == 0) {
            snmp_version = 3;

            // Expect additional parameters for SNMPv3
            if (argc > 2) {
                allowed_community = argv[2];
            } else {
                printf("Usage: %s 3 <username> [noAuthNoPriv|authNoPriv|authPriv] [authProtocol authPassword [privProtocol privPassword]]\n", argv[0]);
                exit(EXIT_FAILURE);
            }

            if (argc > 3) {
                security_level = argv[3];
            }

            if (strcmp(security_level, "authNoPriv") == 0 || strcmp(security_level, "authPriv") == 0) {
                if (argc > 4) {
                    authProtocol = argv[4];
                    authPassword = argv[5];
                } else {
                    printf("Authentication parameters required for security levels 'authNoPriv' or 'authPriv'\n");
                    exit(EXIT_FAILURE);
                }
            }

            if (strcmp(security_level, "authPriv") == 0) {
                if (argc > 6) {
                    privProtocol = argv[6];
                    privPassword = argv[7];
                } else {
                    printf("Privacy parameters required for security level 'authPriv'\n");
                    exit(EXIT_FAILURE);
                }
            }
        } else {
            printf("Usage: %s [1|2c|3] [community|username] [security_level] [authProtocol authPassword [privProtocol privPassword]]\n", argv[0]);
            exit(EXIT_FAILURE);
        }

        if (snmp_version == 1 || snmp_version == 2) {
            if (argc > 2) {
                allowed_community = argv[2];
            }
        }
    } else {
        printf("Usage: %s [1|2c|3] [community|username] [security_level] [authProtocol authPassword [privProtocol privPassword]]\n", argv[0]);
        printf("Using default SNMP version 1 and community 'public'\n");
    }
    
    FILE *file = fopen("CAMERA-MIB.txt", "r");
    if (!file) {
        perror("Error opening file");
        return 1;
    }  

    // 주요 Public MIB 노드들을 하드코딩으로 추가
    add_mib_node("sysDescr", "1.3.6.1.2.1.1.1.0", "DisplayString", HANDLER_CAN_RONLY, "current", 
                "IP Camera", NULL);

    add_mib_node("sysObjectID", "1.3.6.1.2.1.1.2.0", "OBJECT IDENTIFIER", HANDLER_CAN_RONLY, "current", 
                 "1.3.6.1.4.1.127.1.9", NULL);
    unsigned long uptime = get_system_uptime();
    add_mib_node("sysUpTime", "1.3.6.1.2.1.1.3.0", "TimeTicks", HANDLER_CAN_RONLY, "current", 
                &uptime, NULL);
    add_mib_node("sysContact", "1.3.6.1.2.1.1.4.0", "DisplayString", HANDLER_CAN_RWRITE, "current", 
                "admin@example.com", NULL);
    add_mib_node("sysName", "1.3.6.1.2.1.1.5.0", "DisplayString", HANDLER_CAN_RWRITE, "current", 
                "EN675", NULL);
    root = add_mib_node("cam", "1.3.6.1.4.1.127.1", "MODULE-IDENTITY", 0, "current", "", NULL);

    char line[256];

    while (fgets(line, sizeof(line), file)) {
        if (strstr(line, "IMPORTS")) {
            while (!strstr(line, ";")) {
                fgets(line, sizeof(line), file);
            }
            continue;
        }

        if (strstr(line, "OBJECT IDENTIFIER")) {
            parse_object_identifier(line);
        }

        if (strstr(line, "OBJECT-TYPE")) {
            parse_object_type(line, file);
        }
    }

    fclose(file);

    int cpu_usage = get_cpuUsage();
    int memory_usage = get_memory_usage();
    
    // -- System Information
    update_mib_node_value("modelName", "eyenix EN675");
    update_mib_node_value("versionInfo", get_version());
    update_mib_node_value("dateTimeInfo", get_date());
    update_mib_node_value("cpuUsage", &cpu_usage);
    update_mib_node_value("cpuLoad1Min", get_cpu_load(1));
    update_mib_node_value("cpuLoad5Min", get_cpu_load(5));
    update_mib_node_value("cpuLoad15Min", get_cpu_load(15));

    // -- Network Information
    update_mib_node_value("macAddressInfo", get_mac_address());
    update_mib_node_value("ipAddressInfo", get_current_ip());
    update_mib_node_value("gateway", get_current_gateway());
    update_mib_node_value("subnetMask", get_current_netmask());

    // -- Storage Information
    update_mib_node_value("flashStatus", check_flash_memory_installed());
    update_mib_node_value("memoryusage", &memory_usage);
    update_mib_node_value("sdCardStatus", check_sdcard_installed());
    // update_mib_node_value("sdCardCapacity", get_version());

    // print_all_mib_nodes();

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }

    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = INADDR_ANY;
    servaddr.sin_port = htons(SNMP_PORT);

    if (bind(sockfd, (const struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    while (1) {
        socklen_t len = sizeof(cliaddr);
        int n = recvfrom(sockfd, (char *)buffer, BUFFER_SIZE, 0, (struct sockaddr *)&cliaddr, &len);

        snmp_request(buffer, n, &cliaddr, sockfd, snmp_version, allowed_community);
    }

    free_mib_nodes();

    return 0;
}
